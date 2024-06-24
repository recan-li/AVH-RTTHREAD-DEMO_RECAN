#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "rpc_fs.h"
#include "base64.h"

#if (CFG_AWS_IOT_SOCKET_ENABLE)
#include "iot_socket.h"
#define AF_INET                     IOT_SOCKET_AF_INET
#define SOCK_STREAM                 IOT_SOCKET_SOCK_STREAM
#define SOCK_DGRAM                  IOT_SOCKET_SOCK_DGRAM
#define socket(af, type, protocol)  iotSocketCreate(af, type, protocol)
#define listen(fd, backlog)         iotSocketListen (fd, backlog)
#define read(fd, buf, len)          iotSocketRecv(fd, buf, len)
#define write(fd, buf, len)         iotSocketSend(fd, buf, len)
#define close(fd)                   iotSocketClose(fd)
#define INET_ADDRSTRLEN             128
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define DEBUG_MODE      1
#define TCP_MODE        0

#define SERVER_IP       "127.0.0.1"
#define SERVER_IP_BYTES {0x7F, 0x00, 0x00, 0x01} // sepcial for AVH socket API

#if (TCP_MODE)
#define SERVER_PORT     55557
#else
#define SERVER_PORT     55558
#endif

#ifdef __RTTHREAD__
#include <rtthread.h>
#define MAX_READ_BLOCK_SIZE             (24 * 1024)
#define MAX_SEND_BLOCK_LEN              (20 * 1024)
#else
// maybe itn't the best !!!
#define MAX_READ_BLOCK_SIZE             (24 * 1024)
#define MAX_SEND_BLOCK_LEN              (40 * 1024)
#endif

#define BASE64_LEN(len)                 ((len + 2 ) / 3 * 4)
#define MAX_READ_BLOCK_BASE64_SIZE      BASE64_LEN(MAX_READ_BLOCK_SIZE)

static int g_sock = -1;
static int g_fd = 1;
#if !(CFG_AWS_IOT_SOCKET_ENABLE)
static struct sockaddr_in g_serv_addr;
#endif

#include <stdio.h>
#include <string.h>
 
int log_hexdump(const char *title, const unsigned char *data, int len)
{
    char str[160], octet[10];
    int ofs, i, k, d;
    const unsigned char *buf = (const unsigned char *)data;
    const char dimm[] = "+------------------------------------------------------------------------------+";
 
    printf("%s (%d bytes):\r\n", title, len);
    printf("%s\r\n", dimm);
    printf("| Offset  : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F   0123456789ABCDEF |\r\n");
    printf("%s\r\n", dimm);
 
    for (ofs = 0; ofs < (int)len; ofs += 16) {
        d = snprintf( str, sizeof(str), "| %08X: ", ofs );
 
        for (i = 0; i < 16; i++) {
            if ((i + ofs) < (int)len) {
                snprintf( octet, sizeof(octet), "%02X ", buf[ofs + i] );
            } else {
                snprintf( octet, sizeof(octet), "   " );
            }
 
            d += snprintf( &str[d], sizeof(str) - d, "%s", octet );
        }
        d += snprintf( &str[d], sizeof(str) - d, "  " );
        k = d;
 
        for (i = 0; i < 16; i++) {
            if ((i + ofs) < (int)len) {
                str[k++] = (0x20 <= (buf[ofs + i]) &&  (buf[ofs + i]) <= 0x7E) ? buf[ofs + i] : '.';
            } else {
                str[k++] = ' ';
            }
        }
 
        str[k] = '\0';
        printf("%s |\r\n", str);
    }
 
    printf("%s\r\n", dimm);
 
    return 0;
}

int log_hexdump2(const char *title, const unsigned char *data, int len)
{
    int i = 0;

    printf("%s: ", title);
    for (i = 0; i < len; i++) {
        printf("%02X", data[i]);
    }
    printf("\n");

    return 0;
}

int ai_bridge_init(void)
{
#if (CFG_AWS_IOT_SOCKET_ENABLE)
#if (TCP_MODE)
    if ((g_sock = iotSocketCreate(AF_INET, IOT_SOCKET_SOCK_STREAM, IOT_SOCKET_IPPROTO_TCP)) < 0) {
#else
    if ((g_sock = iotSocketCreate(AF_INET, IOT_SOCKET_SOCK_DGRAM, IOT_SOCKET_IPPROTO_UDP)) < 0) {
#endif
        return -1;
    }
    //printf("g_sock: %d\n", g_sock);    
#else
    if ((g_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return -1;
    }

    memset(&g_serv_addr, 0, sizeof(g_serv_addr));
    g_serv_addr.sin_family = AF_INET;
    g_serv_addr.sin_port = htons(SERVER_PORT);
    g_serv_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    //printf("%s:%d\n", SERVER_IP, SERVER_PORT);
#endif

#if (TCP_MODE)
#if (CFG_AWS_IOT_SOCKET_ENABLE)
    uint8_t ip_array[] = SERVER_IP_BYTES;
    uint32_t ip_len = sizeof(ip_array);
    int ret = iotSocketConnect (g_sock, (const uint8_t *)ip_array, ip_len, SERVER_PORT);
    //printf("ret: %d\n", ret);
#else
    if (connect(g_sock, (struct sockaddr *)&g_serv_addr, sizeof(g_serv_addr)) < 0) {
        close(g_sock);
        printf("connect fail\n");
        return -1;
    }
#endif
#endif

    //printf("g_sock: %d\n", g_sock);
    return g_sock;
}

int ai_bridge_deinit(void)
{
    close(g_sock);
    return 0;
}

static cJSON *ai_bridge_cJSON_exhcange(cJSON *in)
{
    //printf("%s:%d %d %d\n", __func__, __LINE__, 0, SERVER_PORT);
    char *json_string = cJSON_Print(in);
    if (json_string) {
#if (DEBUG_MODE)
        if (strlen(json_string) <= 2048) {
            printf(">>> %s\n", json_string);
        } else {
            char tmp[1024] = {0};
            memcpy(tmp, json_string, sizeof(tmp) - 1);
            printf("tmp: %s\n", tmp);
        }
#endif
#if (CFG_AWS_IOT_SOCKET_ENABLE)
        //printf("%s:%d %d\n", __func__, __LINE__, g_sock);
        //extern int32_t iotSocketSendTo (int32_t socket, const void *buf, \
            uint32_t len, const uint8_t *ip, uint32_t ip_len, uint16_t port);
#if (TCP_MODE)
        int ret = iotSocketSend(g_sock, json_string, strlen(json_string));
#else
        uint8_t ip_array[] = SERVER_IP_BYTES;
        uint32_t ip_len = sizeof(ip_array);
        //printf("%d %d %d %d %d\n", ip_array[0], ip_array[1], ip_array[2], ip_array[3], ip_len);
        int ret = iotSocketSendTo (g_sock, json_string, strlen(json_string), \
            (const uint8_t *)ip_array, ip_len, SERVER_PORT);
#endif
        //printf("%s:%d %d %d\n", __func__, __LINE__, ret, SERVER_PORT);

#else
#if (TCP_MODE)
        send(g_sock, json_string, strlen(json_string), 0);
#else
        sendto(g_sock, json_string, strlen(json_string), 0, \
            (struct sockaddr *)&g_serv_addr, sizeof(g_serv_addr));
#endif
#endif
        free(json_string);

        //printf("%s:%d %d %d %d\n", __func__, __LINE__, 0, SERVER_PORT, g_sock);
    }
    
    // 释放 cJSON 对象
    cJSON_Delete(in);

    char buffer[1024] = {0};
#if (CFG_AWS_IOT_SOCKET_ENABLE)
    uint8_t ip_recv[64];
    uint16_t port = 0;
    uint32_t ip_len = sizeof(ip_recv);
#if (TCP_MODE)
    int ret = iotSocketRecv (g_sock, buffer, sizeof(buffer));
#else
    int ret = iotSocketRecvFrom (g_sock, buffer, sizeof(buffer), ip_recv, &ip_len, &port);
#endif
#else
#if (TCP_MODE)
    int ret = recv(g_sock, buffer, sizeof(buffer), 0);
#else
    int ret = recvfrom(g_sock, buffer, sizeof(buffer), 0, NULL, NULL);
#endif
#endif

#if (DEBUG_MODE)
    printf("<<< %s\n", buffer);
#endif

    if (ret < 0) {
        return NULL;
    }

    cJSON * rsp_root = NULL;
    cJSON * status = NULL;
    RPC_FILE *file = NULL;

    rsp_root = cJSON_Parse(buffer);
    if (!rsp_root) {
        printf("error null rsp\n");
        return NULL;
    }

    status = cJSON_GetObjectItem(rsp_root, "status");

    if (strcmp(status->valuestring, "ok")) {
        printf("status err: %s\n", status->valuestring);
        cJSON_Delete(rsp_root);
        return NULL;
    } 

    return rsp_root;
}

int ai_bridge_baidu_stt(const char *audio_file, char *speech_in, char *speech_out)
{
    int i;
    int ret;
    char *buffer = NULL;
    char *p = NULL;
    RPC_FILE *file = NULL;

    rpc_fs_init();

    int size = rpc_fs_fsize(audio_file);
    int base64_len = BASE64_LEN(size);
    //printf("size: %d %d\n", size, base64_len);

    buffer = (char *)malloc(base64_len + 1);
    p = buffer;

    int cnt = size / MAX_READ_BLOCK_SIZE;
    int left = size % MAX_READ_BLOCK_SIZE;

    file = rpc_fs_fopen(audio_file, "rb+");

    for (i = 0; i < cnt; i++) {
        ret = rpc_fs_fread(p, 1, MAX_READ_BLOCK_SIZE, file);
        //printf("ret: %d\n", ret);
        if (i == 0) {
            //log_hexdump("frist", p, MAX_READ_BLOCK_SIZE);
            //log_hexdump2("frist", p, MAX_READ_BLOCK_SIZE);
        }
        base64_encode_tail((const uint8_t *)p, MAX_READ_BLOCK_SIZE, p);
        if (i == 0) {
            //printf("p: (%d)%s\n", (int)strlen(p), p);;
        }
        p += MAX_READ_BLOCK_BASE64_SIZE;
    }

    if (left != 0) {
        ret = rpc_fs_fread(p, 1, left, file);
        //printf("ret: %d %d\n", ret, left);
        base64_encode_tail((const uint8_t *)p, left, p);
        //printf("p: (%d)%s\n", (int)strlen(p), p);
        p += BASE64_LEN(left);
        *p = '\0'; 
    }

    //printf("len: %d\n", (int)strlen(buffer));

    rpc_fs_fclose(file);

    rpc_fs_deinit();

    ai_bridge_init();

    // send many times MAX_SEND_BLOCK_LEN
    int total_len = strlen((char *)buffer);
    int have_sent = 0;
    int max_part = 0;
    cJSON *root = NULL;
    cJSON *rsp_root = NULL;
    p = buffer; 

    cnt = total_len / MAX_SEND_BLOCK_LEN;
    left = total_len % MAX_SEND_BLOCK_LEN;
    //printf("%d %d %d\n", total_len, cnt, left);

    if (left != 0) {
        max_part = cnt + 1;
    } else {
        max_part = cnt;
    }

    for (i = 0; i < cnt; i++) {
        char c_bak = *(p + MAX_SEND_BLOCK_LEN);
        *(p + MAX_SEND_BLOCK_LEN) = '\0';
        root = cJSON_CreateObject();
    
        cJSON_AddItemToObject(root, "operation", cJSON_CreateString("baidu_stt"));
        cJSON_AddItemToObject(root, "cur_part", cJSON_CreateNumber(i + 1));
        cJSON_AddItemToObject(root, "max_part", cJSON_CreateNumber(max_part));
        cJSON_AddItemToObject(root, "file_len", cJSON_CreateNumber(size));
        cJSON_AddItemToObject(root, "base64", cJSON_CreateString((char *)p));
        *(p + MAX_SEND_BLOCK_LEN) = c_bak;
        p += MAX_SEND_BLOCK_LEN;
        
        rsp_root = ai_bridge_cJSON_exhcange(root);

        if (i + 1 != max_part) {
            cJSON_Delete(rsp_root);
        }        
    }

    // the lastest part
    if (left != 0) {
        char c_bak = *(p + left);
        *(p + left) = '\0';
        root = cJSON_CreateObject();
    
        cJSON_AddItemToObject(root, "operation", cJSON_CreateString("baidu_stt"));
        cJSON_AddItemToObject(root, "cur_part", cJSON_CreateNumber(i + 1));
        cJSON_AddItemToObject(root, "max_part", cJSON_CreateNumber(max_part));
        cJSON_AddItemToObject(root, "file_len", cJSON_CreateNumber(size));
        cJSON_AddItemToObject(root, "base64", cJSON_CreateString((char *)p));
        *(p + left) = c_bak;
        p += left;
        
        rt_kprintf("%s:%d ...\n", __func__, __LINE__);
        rsp_root = ai_bridge_cJSON_exhcange(root);
    } 

    rt_kprintf("%s:%d ...\n", __func__, __LINE__);

    if (rsp_root) {
        cJSON *text_req_obj = cJSON_GetObjectItem(rsp_root, "text_req");
        if (text_req_obj) {
            printf("text req: %s\n", text_req_obj->valuestring);
            strcpy(speech_in, text_req_obj->valuestring);
            ret = 0;
        }
        cJSON *text_rsp_obj = cJSON_GetObjectItem(rsp_root, "text_rsp");
        if (text_req_obj) {
            printf("text rsp: %s\n", text_rsp_obj->valuestring);
            strcpy(speech_out, text_rsp_obj->valuestring);
            ret = 0;
        }        
        rt_kprintf("%s:%d ...\n", __func__, __LINE__);
        cJSON_Delete(rsp_root);
    } else {
        ret = -1;
    }

    rt_kprintf("%s:%d ...\n", __func__, __LINE__);

    ai_bridge_deinit();

    free(buffer); 

    rt_kprintf("%s:%d ...\n", __func__, __LINE__);

    return ret;
}

#ifndef AI_MAIN
int ai_bridge_main(int argc, const char *argv[])
#else
int main(int argc, const char *argv[])
#endif
{   
    char text_in[128] = {0}; 
    char text_out[2048] = {0};
    const char *file_name = "123.txt";

    printf("%s:%d ...\n", __func__, __LINE__);
    rt_kprintf("%s:%d ...\n", __func__, __LINE__);

    if (argc > 1) {
        file_name = argv[1];
    }

    ai_bridge_baidu_stt(file_name, text_in, text_out);

#ifdef __RTTHREAD__
    rt_kprintf(">>> %s\n", text_in);
    rt_kprintf("<<< %s\n", text_out);
#endif

    return 0;
}
