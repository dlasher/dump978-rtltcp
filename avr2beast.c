#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
#include <errno.h>
#include <stdint.h>

#define HEARTBEAT_INTERVAL 30
#define LINE_BUF_SIZE 4096

#define BEAST_ESC     0x1a
#define BEAST_STATUS  0x14
#define BEAST_MODE_S  0x32

#define AVR_PREFIX_HEX_LEN 14
#define FRAME_HEX_LEN_SHORT 28

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int decode_hex(const char *hex, int hexlen, uint8_t *out, int outsz)
{
    int i, pos = 0;
    for (i = 0; i + 1 < hexlen && pos < outsz; i += 2) {
        int hi = hex_val(hex[i]);
        int lo = hex_val(hex[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[pos++] = (hi << 4) | lo;
    }
    return pos;
}

static void write_ts(struct timeval *tv)
{
    uint32_t sec = tv->tv_sec;
    uint16_t frac = (uint16_t)((tv->tv_usec * 128LL + 500000) / 1000000);
    putchar((sec >> 24) & 0xff);
    putchar((sec >> 16) & 0xff);
    putchar((sec >> 8) & 0xff);
    putchar(sec & 0xff);
    putchar((frac >> 8) & 0xff);
    putchar(frac & 0xff);
}

static void send_heartbeat(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    putchar(BEAST_ESC);
    putchar(BEAST_STATUS);
    write_ts(&tv);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    int hb_interval = HEARTBEAT_INTERVAL;
    char line[LINE_BUF_SIZE];
    struct timeval last_activity;

    if (argc > 1) {
        int n = atoi(argv[1]);
        if (n > 0) hb_interval = n;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    gettimeofday(&last_activity, NULL);

    while (1) {
        struct timeval now, timeout;
        fd_set rfds;
        int ret;
        long elapsed;

        gettimeofday(&now, NULL);
        elapsed = now.tv_sec - last_activity.tv_sec;

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        timeout.tv_sec = (elapsed >= hb_interval) ? 0 : (hb_interval - elapsed);
        timeout.tv_usec = 0;

        ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &timeout);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) {
            send_heartbeat();
            gettimeofday(&last_activity, NULL);
            continue;
        }

        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (feof(stdin)) break;
            continue;
        }

        int linelen = strlen(line);
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            line[--linelen] = '\0';

        if (linelen < 3 || line[0] != '<' || line[linelen - 1] != ';')
            continue;

        const char *hex = line + 1;
        int hexlen = linelen - 2;

        if (hexlen < AVR_PREFIX_HEX_LEN + FRAME_HEX_LEN_SHORT)
            continue;

        const char *frame_hex = hex + AVR_PREFIX_HEX_LEN;
        int frame_hex_len = hexlen - AVR_PREFIX_HEX_LEN;

        uint8_t frame[256];
        int framelen = decode_hex(frame_hex, frame_hex_len, frame, sizeof(frame));
        if (framelen < 1) continue;

        struct timeval tv;
        gettimeofday(&tv, NULL);

        putchar(BEAST_ESC);
        putchar(BEAST_MODE_S);
        write_ts(&tv);
        putchar(0);
        fwrite(frame, 1, framelen, stdout);
        fflush(stdout);

        gettimeofday(&last_activity, NULL);
    }

    fflush(stdout);
    return 0;
}
