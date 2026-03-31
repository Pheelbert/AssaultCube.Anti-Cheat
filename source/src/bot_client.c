// bot_client.c - Headless bot that connects to an AC server as a player.
// Keeps it simple: one bot per process, exit on failure (Docker restarts).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <enet/enet.h>

// --- Protocol constants (match protocol.h enum order) ---
enum {
    SV_SERVINFO=0, SV_SERVINFO_RESPONSE, SV_SERVINFO_CONTD, SV_WELCOME,
    SV_INITCLIENT, SV_POS, SV_POSC, SV_POSC2, SV_POSC3, SV_POSC4,
    SV_POSN, SV_TEXT, SV_TEAMTEXT, SV_TEXTME, SV_TEAMTEXTME, SV_TEXTPRIVATE,
    SV_SOUND, SV_VOICECOM, SV_VOICECOMTEAM, SV_CDIS,
    SV_SHOOT, SV_EXPLODE, SV_SUICIDE, SV_AKIMBO, SV_RELOAD,
    SV_GIBDIED, SV_DIED, SV_GIBDAMAGE, SV_DAMAGE, SV_HITPUSH, SV_SHOTFX, SV_THROWNADE,
    SV_TRYSPAWN, SV_SPAWNSTATE, SV_SPAWN, SV_SPAWNDENY, SV_FORCEDEATH, SV_RESUME,
    SV_DISCSCORES, SV_TIMEUP, SV_EDITENT, SV_ITEMACC,
    SV_MAPCHANGE, SV_ITEMSPAWN, SV_ITEMPICKUP,
    SV_PING, SV_PONG, SV_CLIENTPING,
    SV_EDITMODE, SV_EDITXY, SV_EDITARCH, SV_EDITBLOCK, SV_EDITD, SV_EDITE, SV_NEWMAP,
    SV_SENDMAP, SV_RECVMAP, SV_REMOVEMAP,
    SV_SERVMSG, SV_SERVMSGVERB, SV_ITEMLIST, SV_WEAPCHANGE, SV_PRIMARYWEAP,
    SV_FLAGACTION, SV_FLAGINFO, SV_FLAGMSG, SV_FLAGCNT,
    SV_ARENAWIN,
    SV_SETADMIN, SV_SERVOPINFO,
    SV_CALLVOTE, SV_CALLVOTESUC, SV_CALLVOTEERR, SV_VOTE, SV_VOTERESULT,
    SV_SETTEAM, SV_TEAMDENY, SV_SERVERMODE,
    SV_IPLIST, SV_SPECTCN,
    SV_LISTDEMOS, SV_SENDDEMOLIST, SV_GETDEMO, SV_SENDDEMO, SV_DEMOPLAYBACK,
    SV_CONNECT,
    SV_SWITCHNAME, SV_SWITCHSKIN, SV_SWITCHTEAM,
    SV_CLIENT, SV_EXTENSION, SV_MAPIDENT, SV_DEMOCHECKSUM, SV_DEMOSIGNATURE,
    SV_PAUSEMODE, SV_GETVITA, SV_VITADATA, SV_HASHVERIFY, SV_ANTICHEAT_TELEMETRY,
    SV_NUM
};

#define AC_VERSION       (-1302)
#define GUN_ASSAULT      6
#define NUMGUNS          9
#define DMF              16.0f
#define MAXTRANS         5000

// --- Packet buffer ---
typedef struct { unsigned char d[MAXTRANS]; int len, pos; } buf_t;
static void buf_init(buf_t *b) { b->len = b->pos = 0; }
static void buf_put(buf_t *b, unsigned char v) { if(b->len < MAXTRANS) b->d[b->len++] = v; }
static unsigned char buf_get(buf_t *b) { return b->pos < b->len ? b->d[b->pos++] : 0; }

static void putint(buf_t *p, int n) {
    if(n >= -127 && n < 128) buf_put(p, n & 0xFF);
    else if(n >= -0x8000 && n < 0x8000) { buf_put(p, 0x80); buf_put(p, n & 0xFF); buf_put(p, (n>>8) & 0xFF); }
    else { buf_put(p, 0x81); buf_put(p, n&0xFF); buf_put(p, (n>>8)&0xFF); buf_put(p, (n>>16)&0xFF); buf_put(p, (n>>24)&0xFF); }
}
static int getint(buf_t *p) {
    int c = (signed char)buf_get(p);
    if(c == -128) { int n = (unsigned char)buf_get(p); n |= ((signed char)buf_get(p)) << 8; return n; }
    if(c == -127) { int n = (unsigned char)buf_get(p); n |= (unsigned char)buf_get(p)<<8; n |= (unsigned char)buf_get(p)<<16; n |= (unsigned char)buf_get(p)<<24; return n; }
    return c;
}
static void putuint(buf_t *p, unsigned int n) {
    if(n < 128) buf_put(p, n);
    else if(n < (1<<14)) { buf_put(p, 0x80|(n&0x7F)); buf_put(p, n>>7); }
    else if(n < (1<<21)) { buf_put(p, 0x80|(n&0x7F)); buf_put(p, 0x80|((n>>7)&0x7F)); buf_put(p, n>>14); }
    else { buf_put(p, 0x80|(n&0x7F)); buf_put(p, 0x80|((n>>7)&0x7F)); buf_put(p, 0x80|((n>>14)&0x7F)); buf_put(p, n>>21); }
}
static unsigned int getuint(buf_t *p) {
    unsigned int n = buf_get(p);
    if(n & 0x80) { n &= 0x7F; unsigned int b = buf_get(p); if(b & 0x80) { n |= (b&0x7F)<<7; unsigned int c = buf_get(p); if(c & 0x80) { n |= (c&0x7F)<<14; n |= buf_get(p)<<21; } else n |= c<<14; } else n |= b<<7; }
    return n;
}
static void sendstr(buf_t *p, const char *s) { while(*s) putint(p, *s++); putint(p, 0); }
static void skipstr(buf_t *p) { while(p->pos < p->len && getint(p) != 0); }
static void skip(buf_t *p, int n) { p->pos += n; if(p->pos > p->len) p->pos = p->len; }

static ENetPacket *pkt(buf_t *p, int flags) { return enet_packet_create(p->d, p->len, flags); }

// --- Bot state ---
static int my_cn = -1, lifeseq = 0, gun = GUN_ASSAULT, alive = 0;
static int map_gzs = 0, map_rev = 0; // for SV_MAPIDENT
static float bx = 128, by = 128, bz = 4, yaw = 0, target_yaw = 0;
static enet_uint32 last_pos = 0, last_ping = 0, last_spawn = 0;

// --- Send helpers ---
static void send_servinfo_resp(ENetPeer *peer) {
    buf_t p; buf_init(&p);
    putint(&p, SV_SERVINFO_RESPONSE);
    putint(&p, 0); putint(&p, (int)(time(NULL)/60)); putint(&p, 0);
    buf_put(&p,0); buf_put(&p,0); buf_put(&p,0); buf_put(&p,0); // ip
    putint(&p, 0); // wantauth=0
    enet_peer_send(peer, 1, pkt(&p, ENET_PACKET_FLAG_RELIABLE));
}
static void send_connect(ENetPeer *peer, const char *name) {
    buf_t p; buf_init(&p);
    putint(&p, SV_CONNECT); putint(&p, AC_VERSION); putint(&p, 0x04);
    sendstr(&p, name); sendstr(&p, ""); sendstr(&p, "");
    putint(&p, 0); putint(&p, GUN_ASSAULT);
    putint(&p, 0); putint(&p, 0); // skins
    putint(&p, 0); putint(&p, 0); putint(&p, 90); putint(&p, 50);
    enet_peer_send(peer, 1, pkt(&p, ENET_PACKET_FLAG_RELIABLE));
}
static void send_tryspawn(ENetPeer *peer) {
    buf_t p; buf_init(&p); putint(&p, SV_TRYSPAWN);
    enet_peer_send(peer, 1, pkt(&p, ENET_PACKET_FLAG_RELIABLE));
}
static void send_spawn(ENetPeer *peer) {
    buf_t p; buf_init(&p); putint(&p, SV_SPAWN); putint(&p, lifeseq); putint(&p, gun);
    enet_peer_send(peer, 1, pkt(&p, ENET_PACKET_FLAG_RELIABLE));
}
static void send_pos(ENetPeer *peer) {
    buf_t p; buf_init(&p);
    putint(&p, SV_POS); putint(&p, my_cn);
    putuint(&p, (unsigned)(bx * DMF)); putuint(&p, (unsigned)(by * DMF)); putuint(&p, (unsigned)(bz * DMF));
    // orientation: yaw(10)|pitch(8)|flags(11) = 29 bits
    int ye = ((int)yaw) & 0x3FF, pe = 90 & 0xFF;
    int fl = (1 << 2) | ((lifeseq & 1) << 6) | (1 << 7); // move fwd + onfloor + lifeseq lsb
    unsigned pk = (unsigned)ye | ((unsigned)pe << 10) | ((unsigned)fl << 18);
    buf_put(&p, pk&0xFF); buf_put(&p, (pk>>8)&0xFF); buf_put(&p, (pk>>16)&0xFF); buf_put(&p, (pk>>24)&0xFF);
    enet_peer_send(peer, 0, pkt(&p, 0));
}
static void send_ping(ENetPeer *peer) {
    buf_t p; buf_init(&p); putint(&p, SV_CLIENTPING); putint(&p, 20);
    enet_peer_send(peer, 1, pkt(&p, ENET_PACKET_FLAG_RELIABLE));
}

// --- Process one packet, skip unknown messages gracefully ---
static void process(ENetPeer *peer, ENetPacket *ep) {
    buf_t p; buf_init(&p);
    int sz = ep->dataLength < MAXTRANS ? (int)ep->dataLength : MAXTRANS;
    memcpy(p.d, ep->data, sz); p.len = sz;

    while(p.pos < p.len) {
        int saved = p.pos;
        int type = getint(&p);
        if(type < 0 || type >= SV_NUM) break;

        switch(type) {
        case SV_SERVINFO: {
            my_cn = getint(&p); int proto = getint(&p); int auth = getint(&p);
            getint(&p); getint(&p); skip(&p, 12); getint(&p); getint(&p); getint(&p); skip(&p, 32);
            printf("  SERVINFO cn=%d proto=%d auth=%d\n", my_cn, proto, auth);
            send_servinfo_resp(peer);
            break;
        }
        case SV_SERVINFO_CONTD: {
            int a = getint(&p);
            if(a) { skip(&p, 64); getint(&p); skipstr(&p); skipstr(&p); }
            printf("  CONTD, sending CONNECT\n");
            send_connect(peer, getenv("BOT_NAME") ? getenv("BOT_NAME") : "Bot");
            break;
        }
        case SV_WELCOME: { int j = getint(&p); printf("  WELCOME joining=%d\n", j); break; }
        case SV_MAPCHANGE: { char m[256]; int i=0; for(;;) { int c=getint(&p); if(!c||i>=255) break; m[i++]=(char)c; } m[i]=0;
            int mode = getint(&p); map_gzs = getint(&p); map_rev = getint(&p); getint(&p);
            printf("  MAP=%s mode=%d gzs=%d rev=%d\n", m, mode, map_gzs, map_rev);
            // Send SV_MAPIDENT to confirm we have the map
            { buf_t mi; buf_init(&mi); putint(&mi, SV_MAPIDENT); putint(&mi, map_gzs); putint(&mi, map_rev);
              enet_peer_send(peer, 1, pkt(&mi, ENET_PACKET_FLAG_RELIABLE)); }
            break; }
        case SV_TIMEUP: getint(&p); getint(&p); break;
        case SV_ITEMLIST: { int it; while((it=getint(&p))>=0) getint(&p); break; }
        case SV_FLAGINFO: { getint(&p); int s=getint(&p); if(s==1) getint(&p); else if(s==2) { getuint(&p); getuint(&p); getuint(&p); } break; }
        case SV_SETTEAM: { int cn=getint(&p); int t=getint(&p); if(cn==my_cn) printf("  TEAM=%d\n", t&0xF); break; }
        case SV_FORCEDEATH: { int cn=getint(&p); if(cn==my_cn && !alive) { printf("  FORCEDEATH\n"); last_spawn=enet_time_get(); } if(cn==my_cn) alive=0; break; }
        case SV_SPAWNSTATE: {
            lifeseq=getint(&p); getint(&p); getint(&p); getint(&p); gun=getint(&p); getint(&p);
            int i; for(i=0;i<NUMGUNS;i++) getint(&p); for(i=0;i<NUMGUNS;i++) getint(&p);
            printf("  SPAWNED lifeseq=%d\n", lifeseq);
            send_spawn(peer); alive=1;
            bx=128+(rand()%64); by=128+(rand()%64); bz=4; yaw=(float)(rand()%360);
            break;
        }
        case SV_SPAWNDENY: getint(&p); last_spawn=enet_time_get(); break;
        case SV_RESUME: { int cn; while((cn=getint(&p))>=0) { int i; for(i=0;i<10;i++) getint(&p); int j; for(j=0;j<NUMGUNS*2;j++) getint(&p); } break; }
        case SV_INITCLIENT: { getint(&p); skipstr(&p); int i; for(i=0;i<8;i++) getint(&p); break; }
        case SV_CDIS: getint(&p); break;
        case SV_SERVERMODE: getint(&p); break;
        case SV_PAUSEMODE: getint(&p); break;
        case SV_PONG: getint(&p); break;
        case SV_PING: { int v=getint(&p); buf_t r; buf_init(&r); putint(&r,SV_PONG); putint(&r,v); enet_peer_send(peer,1,pkt(&r,ENET_PACKET_FLAG_RELIABLE)); break; }
        case SV_GIBDIED: case SV_DIED: { int t=getint(&p); getint(&p); getint(&p); getint(&p); if(t==my_cn) { alive=0; last_spawn=enet_time_get(); printf("  DIED\n"); } break; }
        case SV_SERVMSG: case SV_SERVMSGVERB: skipstr(&p); break;
        case SV_DISCSCORES: { int cn; while((cn=getint(&p))>=0) { skipstr(&p); getint(&p); getint(&p); getint(&p); getint(&p); } break; }
        case SV_SERVOPINFO: getint(&p); getint(&p); break;
        case SV_IPLIST: { int cn; while((cn=getint(&p))>=0) getint(&p); break; }
        case SV_DAMAGE: case SV_GIBDAMAGE: { int i; for(i=0;i<5;i++) getint(&p); break; }
        case SV_HITPUSH: { int i; for(i=0;i<6;i++) getint(&p); break; }
        case SV_SHOTFX: { int i; for(i=0;i<8;i++) getint(&p); break; }
        case SV_ARENAWIN: getint(&p); break;
        case SV_ITEMACC: getint(&p); getint(&p); break;
        case SV_ITEMSPAWN: getint(&p); break;
        case SV_SOUND: getint(&p); getint(&p); break;
        case SV_FLAGMSG: getint(&p); getint(&p); getint(&p); break;
        case SV_FLAGCNT: getint(&p); getint(&p); break;
        case SV_TEXT: case SV_TEAMTEXT: case SV_TEXTME: case SV_TEAMTEXTME: case SV_TEXTPRIVATE: getint(&p); skipstr(&p); break;
        case SV_CLIENT: { getint(&p); int len=getint(&p); skip(&p, len); break; }
        case SV_SWITCHNAME: getint(&p); skipstr(&p); break;
        case SV_SWITCHSKIN: getint(&p); getint(&p); getint(&p); break;
        case SV_SWITCHTEAM: getint(&p); getint(&p); break;
        default: return; // can't parse further
        }
    }
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL); setbuf(stderr, NULL);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    const char *host = getenv("SERVER_HOST");  if(!host) host = "acserver";
    const char *sp   = getenv("SERVER_PORT");  int port = sp ? atoi(sp) : 28763;
    const char *sn   = getenv("BOT_NAME");
    char namebuf[32];
    if(!sn) {
        char hn[64] = {0};
        gethostname(hn, sizeof(hn));
        // Use last few chars of container hostname for unique name
        int l = strlen(hn);
        snprintf(namebuf, sizeof(namebuf), "Bot-%s", l > 4 ? hn + l - 4 : hn);
        sn = namebuf;
    }

    printf("[%s] Starting, server=%s:%d\n", sn, host, port);

    // Wait for server to boot
    printf("[%s] Waiting 30s for server...\n", sn);
    struct timespec w = {30, 0}; nanosleep(&w, NULL);

    if(enet_initialize() != 0) { fprintf(stderr, "enet_initialize failed\n"); return 1; }

    ENetHost *client = enet_host_create(NULL, 1, 3, 0, 0);
    if(!client) { fprintf(stderr, "enet_host_create failed\n"); return 1; }

    ENetAddress addr;
    enet_address_set_host(&addr, host);
    addr.port = (enet_uint16)port;

    ENetPeer *peer = enet_host_connect(client, &addr, 3, 0);
    if(!peer) { fprintf(stderr, "enet_host_connect failed\n"); return 1; }

    printf("[%s] Connecting...\n", sn);

    // Wait for connection (up to 15 seconds)
    ENetEvent event;
    int connected = 0;
    enet_uint32 deadline = enet_time_get() + 15000;
    while(enet_time_get() < deadline) {
        if(enet_host_service(client, &event, 100) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
            connected = 1;
            printf("[%s] Connected!\n", sn);
            break;
        }
    }
    if(!connected) { fprintf(stderr, "[%s] Connection timed out\n", sn); return 1; }

    // Main loop
    last_spawn = enet_time_get();
    for(;;) {
        while(enet_host_service(client, &event, 10) > 0) {
            if(event.type == ENET_EVENT_TYPE_RECEIVE) {
                process(peer, event.packet);
                enet_packet_destroy(event.packet);
            } else if(event.type == ENET_EVENT_TYPE_DISCONNECT) {
                printf("[%s] Disconnected\n", sn);
                return 1; // Docker will restart us
            }
        }

        enet_uint32 now = enet_time_get();

        // Try to spawn every 3 seconds when dead
        if(!alive && now - last_spawn > 3000) {
            send_tryspawn(peer);
            last_spawn = now;
        }

        // Send position when alive
        if(alive && now - last_pos > 100) {
            // Wander
            float dy = target_yaw - yaw;
            if(dy > 180) dy -= 360; if(dy < -180) dy += 360;
            yaw += dy * 0.05f;
            if(yaw >= 360) yaw -= 360; if(yaw < 0) yaw += 360;
            if(now % 4000 < 20) target_yaw = (float)(rand() % 360);
            float r = yaw * 3.14159f / 180.0f;
            bx += cosf(r) * 0.3f; by += sinf(r) * 0.3f;
            if(bx < 32) bx = 32; if(bx > 224) bx = 224;
            if(by < 32) by = 32; if(by > 224) by = 224;

            send_pos(peer);
            last_pos = now;
        }

        // Ping every 2 seconds
        if(now - last_ping > 2000) { send_ping(peer); last_ping = now; }
    }
}
