#include "AutoStart.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>

// ─── Minimal AES-256-CBC + Base64 without OpenSSL ──────────────────────────
// We use a tiny self-contained AES implementation + Base64.
// This avoids adding an OpenSSL dependency just for one feature.

// ---- Base64 ----------------------------------------------------------------
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64Encode(const std::vector<uint8_t>& in)
{
    std::string out;
    out.reserve(((in.size()+2)/3)*4);
    for (std::size_t i=0; i<in.size(); i+=3)
    {
        uint32_t v = (uint32_t)in[i]<<16;
        if (i+1<in.size()) v|=(uint32_t)in[i+1]<<8;
        if (i+2<in.size()) v|=(uint32_t)in[i+2];
        out+=B64[(v>>18)&63];
        out+=B64[(v>>12)&63];
        out+=(i+1<in.size())?B64[(v>>6)&63]:'=';
        out+=(i+2<in.size())?B64[v&63]:'=';
    }
    return out;
}

static std::vector<uint8_t> b64Decode(const std::string& in)
{
    auto val=[](char c)->int{
        if(c>='A'&&c<='Z') return c-'A';
        if(c>='a'&&c<='z') return c-'a'+26;
        if(c>='0'&&c<='9') return c-'0'+52;
        if(c=='+') return 62; if(c=='/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve(in.size()*3/4);
    for (std::size_t i=0; i+3<in.size(); i+=4)
    {
        int v0=val(in[i]),v1=val(in[i+1]),v2=val(in[i+2]),v3=val(in[i+3]);
        if(v0<0||v1<0) break;
        out.push_back((uint8_t)((v0<<2)|(v1>>4)));
        if(in[i+2]!='='&&v2>=0) out.push_back((uint8_t)((v1<<4)|(v2>>2)));
        if(in[i+3]!='='&&v3>=0) out.push_back((uint8_t)((v2<<6)|v3));
    }
    return out;
}

// ---- Tiny AES-256 (ECB block primitive, we build CBC on top) ---------------
// Public domain AES from https://github.com/kokke/tiny-AES-c (inlined subset)
#define AES_BLOCKLEN 16
#define AES_KEYLEN   32
#define AES_keyExpSize 240

typedef struct { uint8_t RoundKey[AES_keyExpSize]; uint8_t Iv[AES_BLOCKLEN]; } AES_ctx;

static const uint8_t sbox[256] = {
 0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
 0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
 0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
 0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
 0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
 0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
 0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
 0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
 0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
 0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
 0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
 0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
 0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
 0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
 0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
 0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

static const uint8_t rsbox[256] = {
 0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
 0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
 0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
 0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
 0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
 0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
 0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
 0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
 0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
 0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
 0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
 0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
 0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
 0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
 0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
 0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d};

static const uint8_t Rcon[11]={0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

static void KeyExpansion(uint8_t* rk, const uint8_t* key)
{
    uint8_t tmp[4]; unsigned i,j; uint8_t k;
    for(i=0;i<8;i++) for(j=0;j<4;j++) rk[i*4+j]=key[i*4+j];
    for(i=8;i<60;i++){
        for(j=0;j<4;j++) tmp[j]=rk[(i-1)*4+j];
        if(i%8==0){
            k=tmp[0]; tmp[0]=sbox[tmp[1]]^Rcon[i/8]; tmp[1]=sbox[tmp[2]];
            tmp[2]=sbox[tmp[3]]; tmp[3]=sbox[k];
        } else if(i%8==4){ for(j=0;j<4;j++) tmp[j]=sbox[tmp[j]]; }
        for(j=0;j<4;j++) rk[i*4+j]=rk[(i-8)*4+j]^tmp[j];
    }
}

static uint8_t xtime(uint8_t x){return (x<<1)^((x>>7)*0x1b);}
static uint8_t mul(uint8_t x,uint8_t y){
    return ((y&1)*x)^((y>>1&1)*xtime(x))^((y>>2&1)*xtime(xtime(x)))^
           ((y>>3&1)*xtime(xtime(xtime(x))))^((y>>4&1)*xtime(xtime(xtime(xtime(x)))));
}

static void AddRoundKey(uint8_t r,uint8_t s[4][4],const uint8_t* rk){
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]^=rk[r*16+i*4+j];
}
static void SubBytes(uint8_t s[4][4]){for(int i=0;i<4;i++)for(int j=0;j<4;j++)s[i][j]=sbox[s[i][j]];}
static void ShiftRows(uint8_t s[4][4]){
    uint8_t t;
    t=s[0][1];s[0][1]=s[1][1];s[1][1]=s[2][1];s[2][1]=s[3][1];s[3][1]=t;
    t=s[0][2];s[0][2]=s[2][2];s[2][2]=t; t=s[1][2];s[1][2]=s[3][2];s[3][2]=t;
    t=s[3][3];s[3][3]=s[2][3];s[2][3]=s[1][3];s[1][3]=s[0][3];s[0][3]=t;
}
static void MixColumns(uint8_t s[4][4]){
    for(int i=0;i<4;i++){
        uint8_t a=s[i][0],b=s[i][1],c=s[i][2],d=s[i][3];
        s[i][0]=mul(a,2)^mul(b,3)^c^d;
        s[i][1]=a^mul(b,2)^mul(c,3)^d;
        s[i][2]=a^b^mul(c,2)^mul(d,3);
        s[i][3]=mul(a,3)^b^c^mul(d,2);
    }
}
static void InvShiftRows(uint8_t s[4][4]){
    uint8_t t;
    t=s[3][1];s[3][1]=s[2][1];s[2][1]=s[1][1];s[1][1]=s[0][1];s[0][1]=t;
    t=s[0][2];s[0][2]=s[2][2];s[2][2]=t; t=s[1][2];s[1][2]=s[3][2];s[3][2]=t;
    t=s[0][3];s[0][3]=s[1][3];s[1][3]=s[2][3];s[2][3]=s[3][3];s[3][3]=t;
}
static void InvSubBytes(uint8_t s[4][4]){for(int i=0;i<4;i++)for(int j=0;j<4;j++)s[i][j]=rsbox[s[i][j]];}
static void InvMixColumns(uint8_t s[4][4]){
    for(int i=0;i<4;i++){
        uint8_t a=s[i][0],b=s[i][1],c=s[i][2],d=s[i][3];
        s[i][0]=mul(a,14)^mul(b,11)^mul(c,13)^mul(d,9);
        s[i][1]=mul(a,9)^mul(b,14)^mul(c,11)^mul(d,13);
        s[i][2]=mul(a,13)^mul(b,9)^mul(c,14)^mul(d,11);
        s[i][3]=mul(a,11)^mul(b,13)^mul(c,9)^mul(d,14);
    }
}

static void AES_ECB_encrypt(const uint8_t* rk, uint8_t* blk)
{
    uint8_t s[4][4];
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]=blk[i*4+j];
    AddRoundKey(0,s,rk);
    for(int r=1;r<14;r++){SubBytes(s);ShiftRows(s);MixColumns(s);AddRoundKey(r,s,rk);}
    SubBytes(s);ShiftRows(s);AddRoundKey(14,s,rk);
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) blk[i*4+j]=s[i][j];
}
static void AES_ECB_decrypt(const uint8_t* rk, uint8_t* blk)
{
    uint8_t s[4][4];
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]=blk[i*4+j];
    AddRoundKey(14,s,rk);
    for(int r=13;r>0;r--){InvShiftRows(s);InvSubBytes(s);AddRoundKey(r,s,rk);InvMixColumns(s);}
    InvShiftRows(s);InvSubBytes(s);AddRoundKey(0,s,rk);
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) blk[i*4+j]=s[i][j];
}

// ---- AES-256-CBC -----------------------------------------------------------
static void makeKey(const std::string& passphrase, uint8_t key[32])
{
    std::memset(key,0,32);
    for(std::size_t i=0;i<passphrase.size()&&i<32;i++)
        key[i]=(uint8_t)passphrase[i];
}

// Static IV for simplicity (deterministic; acceptable for local config)
static const uint8_t STATIC_IV[16]={
    0x52,0x4A,0x6F,0x75,0x72,0x6E,0x61,0x6C,
    0x49,0x56,0x30,0x30,0x31,0x00,0x00,0x00};

std::string AutoStartConfig::Encrypt(const std::string& plain, const std::string& keyStr)
{
    uint8_t key[32]; makeKey(keyStr,key);
    uint8_t rk[AES_keyExpSize]; KeyExpansion(rk,key);

    // PKCS#7 pad to block boundary
    std::size_t plen = ((plain.size()/16)+1)*16;
    std::vector<uint8_t> buf(plen,0);
    std::memcpy(buf.data(),plain.data(),plain.size());
    uint8_t pad=(uint8_t)(plen-plain.size());
    for(std::size_t i=plain.size();i<plen;i++) buf[i]=pad;

    uint8_t iv[16]; std::memcpy(iv,STATIC_IV,16);
    for(std::size_t b=0;b<plen;b+=16)
    {
        for(int i=0;i<16;i++) buf[b+i]^=iv[i];
        AES_ECB_encrypt(rk,buf.data()+b);
        std::memcpy(iv,buf.data()+b,16);
    }
    return b64Encode(buf);
}

std::string AutoStartConfig::Decrypt(const std::string& cipher64, const std::string& keyStr)
{
    std::vector<uint8_t> buf = b64Decode(cipher64);
    if(buf.empty()||buf.size()%16!=0) return "";

    uint8_t key[32]; makeKey(keyStr,key);
    uint8_t rk[AES_keyExpSize]; KeyExpansion(rk,key);

    uint8_t iv[16]; std::memcpy(iv,STATIC_IV,16);
    for(std::size_t b=0;b<buf.size();b+=16)
    {
        uint8_t next[16]; std::memcpy(next,buf.data()+b,16);
        AES_ECB_decrypt(rk,buf.data()+b);
        for(int i=0;i<16;i++) buf[b+i]^=iv[i];
        std::memcpy(iv,next,16);
    }

    // Remove PKCS#7 padding
    if(buf.empty()) return "";
    uint8_t pad=buf.back();
    if(pad==0||pad>16) return "";
    buf.resize(buf.size()-pad);
    return std::string(buf.begin(),buf.end());
}

// ---- JSON serialisation ----------------------------------------------------
bool AutoStartConfig::Load(const std::string& path, const std::string& key)
{
    std::ifstream f(path);
    if(!f) return false;

    Json::Value  root;
    Json::Reader reader;
    if(!reader.parse(f,root)) return false;

    m_sessions.clear();
    const Json::Value& arr = root["sessions"];
    for(const auto& v : arr)
    {
        AutoStartSession s;
        s.profileName  = v.get("profileName","").asString();
        s.host         = v.get("host","").asString();
        s.port         = v.get("port",22).asInt();
        s.user         = v.get("user","").asString();
        s.encryptedPass= v.get("password","").asString();
        s.journalArgs  = v.get("journalArgs","").asString();
        s.filter       = v.get("filter","").asString();
        s.negFilter    = v.get("negFilter","").asString();
        s.useSqlite    = v.get("useSqlite",false).asBool();
        s.dbPath       = v.get("dbPath","").asString();
        s.autoConnect  = v.get("autoConnect",false).asBool();

        // Decrypt password using key
        if(!s.encryptedPass.empty() && !key.empty())
            s.encryptedPass = Decrypt(s.encryptedPass, key);

        m_sessions.push_back(std::move(s));
    }
    return true;
}

bool AutoStartConfig::Save(const std::string& path, const std::string& key) const
{
    Json::Value arr(Json::arrayValue);
    for(const auto& s : m_sessions)
    {
        Json::Value v;
        v["profileName"] = s.profileName;
        v["host"]        = s.host;
        v["port"]        = s.port;
        v["user"]        = s.user;
        // Encrypt the plain password before storing
        v["password"]    = (!s.encryptedPass.empty()&&!key.empty())
                           ? Encrypt(s.encryptedPass,key)
                           : s.encryptedPass;
        v["journalArgs"] = s.journalArgs;
        v["filter"]      = s.filter;
        v["negFilter"]   = s.negFilter;
        v["useSqlite"]   = s.useSqlite;
        v["dbPath"]      = s.dbPath;
        v["autoConnect"] = s.autoConnect;
        arr.append(v);
    }
    Json::Value root;
    root["sessions"] = arr;

    Json::StyledWriter writer;
    std::ofstream f(path);
    if(!f) return false;
    f << writer.write(root);
    return true;
}
