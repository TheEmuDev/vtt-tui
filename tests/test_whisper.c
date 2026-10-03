/* Whispers (docs/WHISPER.md): phones' names, the 'W' and 'N' records, kept
 * whispers, and the GM's :whisper and :players. */

#include "harness.h"

#include <poll.h>
#include <unistd.h>

/* A phone over a raw socket, its hello carrying a name ("" for none). */
typedef struct {
    int       fd;
    WireCatch c;
    WireDec   d;
} Phone;

static void phone_open(Phone *p, Net *n, const char *name)
{
    memset(p, 0, sizeof *p);
    wire_dec_init(&p->d, &WC_SINK, &p->c);
    p->fd = net_connect(n->port);
    char hello[64];
    int  len = snprintf(hello, sizeof hello, name[0] ? "VTT1 %s\n" : "VTT1\n", name);
    CHECK_EQ((int)write(p->fd, hello, (size_t)len), len);
}

/* Reads what has come, pumping the server, until *count reaches want or
 * the wait runs out; 0 when it did. */
static int phone_wait(Phone *p, Net *n, const int *count, int want)
{
    uint8_t buf[8192];
    for (int tries = 0; tries < 40 && *count < want; tries++) {
        net_pump(n, 0);
        struct pollfd pf = { p->fd, POLLIN, 0 };
        if (poll(&pf, 1, 10) <= 0) continue;
        ssize_t got = read(p->fd, buf, sizeof buf);
        if (got <= 0) break;
        wire_dec_feed(&p->d, buf, (size_t)got);
    }
    return *count >= want ? 0 : -1;
}

/* Lets the server and the phones settle, reading whatever came. */
static void settle(Net *n, Phone *ps, int np)
{
    for (int k = 0; k < 6; k++) {
        net_pump(n, 0);
        for (int i = 0; i < np; i++) {
            if (ps[i].fd < 0) continue;
            uint8_t buf[8192];
            struct pollfd pf = { ps[i].fd, POLLIN, 0 };
            while (poll(&pf, 1, 5) > 0) {
                ssize_t got = read(ps[i].fd, buf, sizeof buf);
                if (got <= 0) break;
                wire_dec_feed(&ps[i].d, buf, (size_t)got);
            }
        }
    }
}

/* A browser through /ws with the query given, its frames unwrapped into c. */
static int browser_open(Net *n, const char *query)
{
    int fd = net_connect(n->port);
    char up[400];
    snprintf(up, sizeof up,
             "GET /ws?k=%s%s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", n->code, query);
    CHECK_EQ((int)write(fd, up, strlen(up)), (int)strlen(up));
    return fd;
}

static void browser_read(Net *n, int fd, WireDec *d)
{
    static uint8_t buf[65536];
    size_t len = 0;
    for (int tries = 0; tries < 10; tries++) {
        net_pump(n, 0);
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 10) <= 0) continue;
        ssize_t got = read(fd, buf + len, sizeof buf - len);
        if (got <= 0) break;
        len += (size_t)got;
    }
    /* Past the 101's headers, then binary frames. */
    size_t off = 0;
    for (size_t k = 0; k + 3 < len; k++)
        if (!memcmp(buf + k, "\r\n\r\n", 4)) { off = k + 4; break; }
    while (off + 2 <= len) {
        uint64_t pl = buf[off + 1] & 0x7F;
        size_t   hl = 2;
        if (pl == 126) { pl = ((uint64_t)buf[off + 2] << 8) | buf[off + 3]; hl = 4; }
        if (off + hl + pl > len) break;
        if ((buf[off] & 0x0F) == 2) wire_dec_feed(d, buf + off + hl, (size_t)pl);
        off += hl + (size_t)pl;
    }
}

void test_whisper_net(void)
{
    CASE("a name is trimmed and checked; from a URL, %XX and + decoded first");
    char nm[NET_NAME_MAX];
    CHECK(net_name_clean("Aria", 4, 0, nm) && !strcmp(nm, "Aria"));
    CHECK(net_name_clean("  Aria ", 7, 0, nm) && !strcmp(nm, "Aria"));
    CHECK(!net_name_clean("", 0, 0, nm));
    CHECK(!net_name_clean("   ", 3, 0, nm));
    CHECK(net_name_clean("123456789012345678901234", 24, 0, nm));
    CHECK(!net_name_clean("1234567890123456789012345", 25, 0, nm));
    CHECK(!net_name_clean("A\x01ria", 5, 0, nm));
    CHECK(!net_name_clean("A\x7fria", 5, 0, nm));
    CHECK(!net_name_clean("\xc3", 1, 0, nm));
    CHECK(net_name_clean("Crypt+Ghoul", 11, 1, nm) && !strcmp(nm, "Crypt Ghoul"));
    CHECK(net_name_clean("%C3%89lan", 9, 1, nm) && !strcmp(nm, "\xc3\x89lan"));
    CHECK(!net_name_clean("%C3%8", 5, 1, nm));
    CHECK(!net_name_clean("%zz", 3, 1, nm));
    CHECK(!net_name_clean("%0A", 3, 1, nm));                  /* a newline, decoded, is still refused */
    CHECK(net_name_clean("Crypt+Ghoul", 11, 0, nm) && !strcmp(nm, "Crypt+Ghoul"));

    Renderer r;
    rnd_init(&r);
    rnd_resize(&r, 40, 12);
    rnd_flush(&r, NULL);
    Net n;
    net_init(&n);
    char err[128];
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
    net_set_live(&n, 1);

    CASE("phones say who they are in the hello; the app is told who arrived");
    Phone ps[5];
    phone_open(&ps[0], &n, "Aria");
    phone_open(&ps[1], &n, "Brin");
    phone_open(&ps[2], &n, "");
    settle(&n, ps, 3);
    char who[160], name[NET_NAME_MAX];
    CHECK(net_take_arrival(&n, name, sizeof name) && !strcmp(name, "Aria"));
    CHECK(net_take_arrival(&n, name, sizeof name) && !strcmp(name, "Brin"));
    CHECK(!net_take_arrival(&n, name, sizeof name));         /* the unnamed one is not news */
    CHECK(net_name_seen(&n, "aria") && !net_name_seen(&n, "Cass"));
    net_who(&n, who, sizeof who);
    CHECK(!strcmp(who, "Aria, Brin, 1 terminal"));

    CASE("a whisper goes to its name's phones only, never to another or a nameless one");
    CHECK_EQ(net_whisper(&n, "aria", "The floor is warm", 17, 1, 0), 1);
    CHECK_EQ(phone_wait(&ps[0], &n, &ps[0].c.whispers, 1), 0);
    CHECK(!strcmp(ps[0].c.whisper, "The floor is warm"));
    settle(&n, ps, 3);
    CHECK_EQ(ps[1].c.whispers, 0);
    CHECK_EQ(ps[2].c.whispers, 0);
    net_set_offer(&n, "Aria\nBrin", 9, 0);
    settle(&n, ps, 3);
    CHECK_EQ(ps[0].c.namelists + ps[1].c.namelists + ps[2].c.namelists, 0);   /* names are for browsers */

    CASE("two phones with one name both get it");
    phone_open(&ps[3], &n, "ARIA");
    settle(&n, ps, 4);
    CHECK(net_take_arrival(&n, name, sizeof name) && !strcmp(name, "ARIA"));
    CHECK_EQ(net_whisper(&n, "Aria", "Both of you", 11, 1, 0), 2);
    CHECK_EQ(phone_wait(&ps[3], &n, &ps[3].c.whispers, 1), 0);
    CHECK_EQ(phone_wait(&ps[0], &n, &ps[0].c.whispers, 2), 0);
    net_who(&n, who, sizeof who);
    CHECK(!strcmp(who, "Aria (2), Brin, 1 terminal"));

    CASE("with no phone of the name here it is kept -- the last one -- and given to the first back");
    close(ps[0].fd); ps[0].fd = -1;
    close(ps[3].fd); ps[3].fd = -1;
    settle(&n, ps, 4);
    CHECK_EQ(net_whisper(&n, "Aria", "first", 5, 1, 0), 0);
    CHECK_EQ(net_whisper(&n, "Aria", "second", 6, 1, 0), 0);
    CHECK_EQ(net_whisper(&n, "Aria", "not kept", 8, 0, 0), 0);
    CHECK(n.kept[0].len == 6 && !n.kept[1].len);
    phone_open(&ps[0], &n, "aria");
    CHECK_EQ(phone_wait(&ps[0], &n, &ps[0].c.whispers, 1), 0);
    CHECK(!strcmp(ps[0].c.whisper, "second"));
    CHECK_EQ(n.kept[0].len, (size_t)0);
    settle(&n, ps, 4);
    CHECK_EQ(ps[0].c.whispers, 1);                             /* once */

    CASE("kept whispers outlive a restart; a full table gives up its oldest name");
    for (int k = 0; k < NET_KEPT_MAX + 1; k++) {
        char nk[16], tk[16];
        snprintf(nk, sizeof nk, "P%d", k);
        snprintf(tk, sizeof tk, "to P%d", k);
        net_whisper(&n, nk, tk, strlen(tk), 1, 0);
    }
    CHECK(!strcmp(n.kept[0].name, "P1") && !strcmp(n.kept[NET_KEPT_MAX - 1].name, "P16"));
    net_whisper(&n, "P5", "again", 5, 1, 0);                    /* a name's newer one moves to the end */
    CHECK(!strcmp(n.kept[NET_KEPT_MAX - 1].name, "P5") && !strcmp(n.kept[NET_KEPT_MAX - 1].text, "again"));
    CHECK_EQ(net_whisper(&n, "P2", "", 0, 1, 0), 0);
    CHECK(!strcmp(n.kept[0].name, "P1"));                      /* an empty one keeps nothing */
    for (int i = 0; i < 4; i++) if (ps[i].fd >= 0) { close(ps[i].fd); ps[i].fd = -1; }
    net_set_offer(&n, "Aria\nBrin", 9, 0);
    net_stop(&n);
    CHECK_EQ(net_start(&n, 0, &r, err, sizeof err), 0);
    CHECK(!strcmp(n.kept[0].name, "P1") && net_name_seen(&n, "Brin"));
    CHECK_EQ((int)n.offer_len, 9);
    phone_open(&ps[4], &n, "P3");
    CHECK_EQ(phone_wait(&ps[4], &n, &ps[4].c.whispers, 1), 0);
    CHECK(!strcmp(ps[4].c.whisper, "to P3"));
    close(ps[4].fd);

    CASE("a browser names itself with n= and is offered the names after its FULL, and again on a change");
    WireCatch bc;
    memset(&bc, 0, sizeof bc);
    WireDec bd;
    wire_dec_init(&bd, &WC_SINK, &bc);
    int b = browser_open(&n, "&n=Crypt+Ghoul");
    browser_read(&n, b, &bd);
    CHECK(bc.fulls >= 1);
    CHECK_EQ(bc.namelists, 1);
    CHECK(!strcmp(bc.names, "Aria\nBrin"));
    CHECK(net_take_arrival(&n, name, sizeof name) && !strcmp(name, "P3"));
    CHECK(net_take_arrival(&n, name, sizeof name) && !strcmp(name, "Crypt Ghoul"));
    net_set_offer(&n, "Aria\nBrin", 9, 0);                     /* the same: nothing sent */
    net_set_offer(&n, "Aria", 4, 0);
    browser_read(&n, b, &bd);
    CHECK_EQ(bc.namelists, 2);
    CHECK(!strcmp(bc.names, "Aria"));
    CHECK_EQ(net_whisper(&n, "crypt ghoul", "Psst", 4, 1, 0), 1);
    browser_read(&n, b, &bd);
    CHECK(bc.whispers == 1 && !strcmp(bc.whisper, "Psst"));

    CASE("a bad name in the query is no name, not a refusal");
    WireCatch bc2;
    memset(&bc2, 0, sizeof bc2);
    WireDec bd2;
    wire_dec_init(&bd2, &WC_SINK, &bc2);
    int b2 = browser_open(&n, "&n=%01x");
    browser_read(&n, b2, &bd2);
    CHECK(bc2.fulls >= 1);
    net_who(&n, who, sizeof who);
    CHECK(!strcmp(who, "Crypt Ghoul, 1 unnamed"));
    close(b);
    close(b2);
    net_stop(&n);
    rnd_free(&r);
}

void test_whisper_app(void)
{
    Sandbox sb = sandbox_enter("whisper");
    CHECK_EQ(sb.ok, 1);
    if (!sb.ok) return;
    char path[600];
    snprintf(path, sizeof path, "%s/w.vtt", sb.dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("VTT 10\nname w\nsize 12 6\nzoom 1\ntiles\n", f);
        for (int y = 0; y < 6; y++) fputs("............\n", f);
        fputs("token player 1 1 1 \"Aria\"\ntoken player 3 1 1 \"Crypt\"\ntoken player 5 1 1 \"aria\"\n"
              "token player 7 1 1 \"Spy\"\ntokenhidden\ntoken enemy 9 1 1 \"Ogre\"\n", f);
        fclose(f);
    }
    Renderer r;
    App      a;
    rnd_init(&r);
    rnd_resize(&r, 80, 24);
    app_init(&a, NULL, &r);
    CHECK_EQ(app_open_map(&a, path), 0);
    press(&a, ":play\r");

    CASE(":players and :whisper before :serve say what to do");
    press(&a, ":players\r");
    CHECK(strstr(a.status, ":serve lets the phones join") != NULL);
    press(&a, ":whisper Aria hi\r");
    CHECK(strstr(a.status, "no phone is Aria - :players lists who is here") != NULL);
    CHECK(a.status_gm);

    press(&a, ":serve\r");
    CHECK(net_active(&a.net));

    CASE("the names offered are the players' creatures, once each, none hidden, no enemies");
    app_tick(&a, 0);
    CHECK(a.net.offer_len == strlen("Aria\nCrypt") && !memcmp(a.net.offer, "Aria\nCrypt", a.net.offer_len));
    press(&a, ":B2\rc\025Bryn\r");                     /* Aria relabeled */
    app_tick(&a, 0);
    CHECK(a.net.offer_len == strlen("Bryn\nCrypt\naria") && !memcmp(a.net.offer, "Bryn\nCrypt\naria", a.net.offer_len));

    /* With a creature hidden no status line reaches the players at all,
     * which would hide a whisper said on the wrong side. */
    a.map->tokens.v[3].hidden = 0;
    CASE("a phone arriving by name is said on the GM's line");
    Phone p1, p2;
    phone_open(&p1, &a.net, "Crypt Ghoul");
    phone_open(&p2, &a.net, "Crypt");
    for (int k = 0; k < 6; k++) { net_pump(&a.net, 0); app_tick(&a, 0); }
    CHECK(strstr(a.status, "phone is here") != NULL);
    CHECK(a.status_gm);
    press(&a, ":players\r");
    CHECK(strstr(a.status, "Crypt Ghoul, Crypt") != NULL);
    char *pf = players_text(&a, &r);
    CHECK(strstr(pf, "phone is here") == NULL);
    free(pf);

    CASE(":whisper takes the longest name the line starts with");
    press(&a, ":whisper crypt ghoul The door is a lie\r");
    CHECK(strstr(a.status, "whispered to Crypt Ghoul (1 phone): The door is a lie") != NULL);
    CHECK_EQ(phone_wait(&p1, &a.net, &p1.c.whispers, 1), 0);
    CHECK(!strcmp(p1.c.whisper, "The door is a lie"));
    press(&a, ":whisper Crypt Run\r");
    CHECK_EQ(phone_wait(&p2, &a.net, &p2.c.whispers, 1), 0);
    CHECK(!strcmp(p2.c.whisper, "Run"));
    CHECK_EQ(p1.c.whispers, 1);
    pf = players_text(&a, &r);
    CHECK(strstr(pf, "door is a lie") == NULL && strstr(pf, "whispered") == NULL);
    free(pf);

    CASE("a name never seen is refused, a bare name asks for the text, a sleeper's is kept");
    press(&a, ":whisper Bryn hello\r");
    CHECK(strstr(a.status, "no phone is Bryn") != NULL);    /* offered, but no phone has been Bryn */
    press(&a, ":whisper Crypt\r");
    CHECK(strstr(a.status, ":whisper Crypt TEXT") != NULL);
    press(&a, ":whisper\r");
    CHECK(strstr(a.status, ":whisper NAME TEXT") != NULL);
    close(p1.fd);
    for (int k = 0; k < 6; k++) net_pump(&a.net, 0);
    press(&a, ":whisper Crypt Ghoul Later\r");
    CHECK(strstr(a.status, "Crypt Ghoul's phone is not here - it gets this when it comes back") != NULL);
    press(&a, ":players\r");
    CHECK(strstr(a.status, "a whisper waits for Crypt Ghoul") != NULL);
    press(&a, ":players x\r");
    CHECK(strstr(a.status, ":players lists") != NULL);

    close(p2.fd);
    app_free(&a);
    rnd_free(&r);
    sandbox_leave(&sb);
}
