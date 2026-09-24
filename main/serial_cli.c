#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "hal/uart_ll.h"
#include "hal/usb_serial_jtag_ll.h"
#include "wifi.h"
#include "can.h"
#include "serial_cli.h"

static const char *TAG = "cli";

#define CLI_LINE_MAX  128
#define CLI_POLL_MS   20

// 每个串口一个独立的行缓冲（两个口可同时输入，互不干扰）
typedef struct {
    int  fd;
    char buf[CLI_LINE_MAX];
    size_t len;
    bool last_cr;      // 上一字符是 \r（用于吞掉 \r\n 的 \n）
} cli_port_t;

// ================= 输出辅助 =================

static void print_uptime(uint32_t s)
{
    uint32_t d = s / 86400; s %= 86400;
    uint32_t h = s / 3600;  s %= 3600;
    uint32_t m = s / 60;
    if (d) printf("%lu天%lu小时%lu分%lu秒\r\n",
                  (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
    else if (h) printf("%lu小时%lu分%lu秒\r\n", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    else if (m) printf("%lu分%lu秒\r\n", (unsigned long)m, (unsigned long)s);
    else printf("%lu秒\r\n", (unsigned long)s);
}

static void print_help(void)
{
    printf("\r\n命令列表:\r\n");
    printf("  help              显示本帮助\r\n");
    printf("  info              显示当前设备状态（WiFi/CAN/运行时间）\r\n");
    printf("  status            以单行 JSON 输出状态（供上位机读取）\r\n");
    printf("  mode ap|sta       切换 AP(热点)/STA 模式（保存后重启生效）\r\n");
    printf("  ssid <名称>       设置 STA 连接的 WiFi 名称（保存并立即重连）\r\n");
    printf("  pass <密码>       设置 STA 密码（8~63 字符，保存并立即重连）\r\n");
    printf("  ip                查看当前 IP 配置\r\n");
    printf("  ip auto           自动绑定当前网段的 xxx.xxx.xxx.250\r\n");
    printf("  ip <x.x.x.x>      设置固定 IP 地址\r\n");
    printf("  reboot            重启设备\r\n");
}

static void print_twai_status(void)
{
    twai_status_info_t status;
    if (twai_get_status_info(&status) == ESP_OK) {
        static const char *state_str[] = {"STOPPED", "RUNNING", "BUS_OFF", "RECOVERING"};
        printf("  CAN:     %s（TEC=%lu REC=%lu）\r\n",
               status.state <= TWAI_STATE_RECOVERING ? state_str[status.state] : "?",
               (unsigned long)status.tx_error_counter,
               (unsigned long)status.rx_error_counter);
    } else {
        printf("  CAN:     未初始化\r\n");
    }
}

static void cmd_info(void)
{
    wifi_status_t st;
    wifi_get_status(&st);

    printf("\r\n======== 设备状态 ========\r\n");
    printf("  模式:    %s\r\n",
           st.mode == WIFI_CFG_MODE_AP ? "AP（本机热点）" : "STA（连接路由器）");
    printf("  SSID:    %s\r\n", st.ssid);
    if (st.mode == WIFI_CFG_MODE_AP) {
        printf("  状态:    %s，已连接设备 %d\r\n",
               st.link_up ? "运行中" : "未启动", st.ap_clients);
    } else {
        if (st.link_up) {
            printf("  状态:    已连接（信号 %d dBm）\r\n", st.rssi);
        } else {
            printf("  状态:    未连接\r\n");
        }
    }
    printf("  信道:    %u\r\n", st.channel);
    printf("  IP:      %s\r\n", st.ip);
    printf("  掩码:    %s\r\n", st.netmask);
    printf("  网关:    %s\r\n", st.gw);
    printf("  MAC:     %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           st.mac[0], st.mac[1], st.mac[2], st.mac[3], st.mac[4], st.mac[5]);
    if (wifi_cfg_get_ip_mode() == WIFI_IP_AUTO_250) {
        printf("  IP规则:  自动绑定同网段 xxx.xxx.xxx.250\r\n");
    } else {
        char sip[16];
        wifi_cfg_get_static_ip(sip, sizeof(sip));
        printf("  IP规则:  固定 %s\r\n", sip);
    }
    printf("  mDNS:    http://%s.local\r\n", MDNS_HOSTNAME);
    printf("  运行:    ");
    print_uptime((uint32_t)(esp_timer_get_time() / 1000000ULL));
    uint32_t ring_cnt, total;
    can_get_snapshot(NULL, 0, &ring_cnt, &total);
    printf("  CAN缓冲: %u 条待显示\r\n", (unsigned)ring_cnt);
    print_twai_status();
    printf("==========================\r\n");
}

// ================= status（单行 JSON，供上位机解析） =================

static void json_print_escaped(const char *s)
{
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') putchar('\\');
        putchar(*s);
    }
}

static void cmd_status(void)
{
    wifi_status_t st;
    wifi_get_status(&st);

    char sip[16];
    wifi_cfg_get_static_ip(sip, sizeof(sip));

    uint32_t ring_cnt, total;
    can_get_snapshot(NULL, 0, &ring_cnt, &total);

    const char *twai_state = NULL;
    uint32_t tec = 0, rec = 0;
    twai_status_info_t ts;
    if (twai_get_status_info(&ts) == ESP_OK) {
        static const char *state_str[] = {"STOPPED", "RUNNING", "BUS_OFF", "RECOVERING"};
        twai_state = ts.state <= TWAI_STATE_RECOVERING ? state_str[ts.state] : "?";
        tec = ts.tx_error_counter;
        rec = ts.rx_error_counter;
    }

    printf("{\"mode\":\"%s\",\"link\":%s,\"ssid\":\"",
           st.mode == WIFI_CFG_MODE_AP ? "ap" : "sta",
           st.link_up ? "true" : "false");
    json_print_escaped(st.ssid);
    printf("\",\"rssi\":%d,\"channel\":%u,\"ap_clients\":%d,"
           "\"ip\":\"%s\",\"netmask\":\"%s\",\"gw\":\"%s\","
           "\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
           "\"ipmode\":\"%s\",\"static_ip\":\"%s\",\"mdns\":\"%s\","
           "\"uptime_s\":%lu,\"can_ring\":%u,\"can_total\":%u,",
           st.rssi, st.channel, st.ap_clients,
           st.ip, st.netmask, st.gw,
           st.mac[0], st.mac[1], st.mac[2], st.mac[3], st.mac[4], st.mac[5],
           wifi_cfg_get_ip_mode() == WIFI_IP_AUTO_250 ? "auto" : "static",
           sip, MDNS_HOSTNAME,
           (unsigned long)(esp_timer_get_time() / 1000000ULL),
           (unsigned)ring_cnt, (unsigned)total);
    if (twai_state) {
        printf("\"twai\":\"%s\",\"tec\":%lu,\"rec\":%lu}\r\n",
               twai_state, (unsigned long)tec, (unsigned long)rec);
    } else {
        printf("\"twai\":null,\"tec\":null,\"rec\":null}\r\n");
    }
}

// ================= 命令执行 =================

static void cli_exec(const char *cmd, const char *arg)
{
    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        print_help();
    } else if (!strcmp(cmd, "info")) {
        cmd_info();
    } else if (!strcmp(cmd, "status")) {
        cmd_status();
    } else if (!strcmp(cmd, "mode")) {
        wifi_cfg_mode_t target;
        if (!strcmp(arg, "ap")) target = WIFI_CFG_MODE_AP;
        else if (!strcmp(arg, "sta")) target = WIFI_CFG_MODE_STA;
        else { printf("用法: mode ap | mode sta\r\n"); return; }
        if (wifi_cfg_get_mode() == target) {
            printf("当前已是 %s 模式\r\n",
                   target == WIFI_CFG_MODE_AP ? "AP" : "STA");
            return;
        }
        wifi_cfg_set_mode(target);
        printf("已切换为 %s 模式并保存，设备即将重启生效...\r\n",
               target == WIFI_CFG_MODE_AP ? "AP" : "STA");
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
    } else if (!strcmp(cmd, "ssid")) {
        if (!*arg) { printf("用法: ssid <WiFi名称>\r\n"); return; }
        char pass[65];
        wifi_cfg_get_sta_pass(pass, sizeof(pass));
        if (wifi_cfg_set_sta_credentials(arg, pass) == ESP_OK) {
            printf("SSID 已保存为 \"%s\"，正在重连...\r\n", arg);
        } else {
            printf("设置失败：SSID 需 1~32 字节\r\n");
        }
    } else if (!strcmp(cmd, "pass")) {
        size_t plen = strlen(arg);
        if (plen < 8 || plen > 63) {
            printf("设置失败：密码需 8~63 个字符\r\n");
            return;
        }
        char ssid[33];
        wifi_cfg_get_sta_ssid(ssid, sizeof(ssid));
        if (wifi_cfg_set_sta_credentials(ssid, arg) == ESP_OK) {
            printf("密码已保存，正在重连...\r\n");
        } else {
            printf("设置失败\r\n");
        }
    } else if (!strcmp(cmd, "ip")) {
        if (!*arg) {
            if (wifi_cfg_get_ip_mode() == WIFI_IP_AUTO_250) {
                printf("当前规则: 自动绑定同网段 xxx.xxx.xxx.250\r\n");
            } else {
                char sip[16];
                wifi_cfg_get_static_ip(sip, sizeof(sip));
                printf("当前规则: 固定 IP %s\r\n", sip);
            }
            printf("用法: ip auto | ip <x.x.x.x>\r\n");
        } else if (!strcmp(arg, "auto")) {
            if (wifi_cfg_set_ip_mode(WIFI_IP_AUTO_250, NULL) == ESP_OK) {
                printf("已切换为自动 .250，正在重连绑定...\r\n");
            } else {
                printf("保存失败\r\n");
            }
        } else if (wifi_cfg_set_ip_mode(WIFI_IP_STATIC, arg) == ESP_OK) {
            printf("已设置固定 IP %s，正在重连绑定...\r\n", arg);
        } else {
            printf("IP 地址无效，示例: ip 192.168.1.250\r\n");
        }
    } else if (!strcmp(cmd, "reboot")) {
        printf("设备即将重启...\r\n");
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    } else {
        printf("未知命令 \"%s\"，输入 help 查看帮助\r\n", cmd);
    }
}

// ================= 行接收 =================

static void cli_handle_line(cli_port_t *p)
{
    p->buf[p->len] = '\0';

    char *cmd = p->buf;
    while (*cmd == ' ') cmd++;
    if (!*cmd) goto done;

    char *arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) {
        *arg = '\0';
        arg++;
        arg += strspn(arg, " ");   // 参数前的多余空格跳过（SSID 内部空格保留）
    }
    cli_exec(cmd, arg);

done:
    p->len = 0;
    p->last_cr = false;
}

static void cli_feed(cli_port_t *p, char c)
{
    if (c == '\r' || c == '\n') {
        if (c == '\n' && p->last_cr) {   // \r\n 序列只算一次行结束
            p->last_cr = false;
            return;
        }
        p->last_cr = (c == '\r');
        write(p->fd, "\r\n", 2);
        cli_handle_line(p);
        return;
    }
    p->last_cr = false;

    if (c == 0x08 || c == 0x7f) {        // 退格
        if (p->len > 0) {
            p->len--;
            write(p->fd, "\b \b", 3);
        }
        return;
    }
    if ((unsigned char)c < 0x20 || c == 0x7f) return;   // 忽略其余控制字符

    if (p->len < CLI_LINE_MAX - 1) {
        p->buf[p->len++] = c;
        write(p->fd, &c, 1);             // 回显
    } else {
        write(p->fd, "\a", 1);           // 行满
    }
}

static void cli_task(void *arg)
{
    (void)arg;

    cli_port_t uart_port = { .fd = 0 };
    cli_port_t usj_port  = { .fd = -1 };
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    // 仅用于回显输出；输入不走该 fd——VFS 的非阻塞读在未装 USJ 驱动时
    // 只查驱动环形缓冲（恒为 0），从不排空硬件 FIFO（idf.py: usb_serial_jtag.c
    // get_read_bytes_available），会导致主机写入被 NAK。输入直接轮询 ll FIFO。
    usj_port.fd = open("/dev/secondary", O_RDWR | O_NONBLOCK);
    if (usj_port.fd < 0) {
        ESP_LOGW(TAG, "USB-Serial/JTAG echo fd unavailable (%d)", usj_port.fd);
    }
#endif

    printf("[CLI] 串口配置台就绪（UART0 / USB 串口均可），输入 help 查看命令\r\n");

    uart_dev_t *uart0 = UART_LL_GET_HW(CONFIG_ESP_CONSOLE_UART_NUM);
    uint8_t tmp[64];
    while (1) {
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
        if (usj_port.fd >= 0) {
            uint32_t n = usb_serial_jtag_ll_read_rxfifo(tmp, sizeof(tmp));
            for (uint32_t i = 0; i < n; i++) {
                cli_feed(&usj_port, (char)tmp[i]);
            }
        }
#endif
        uint32_t n = uart_ll_get_rxfifo_len(uart0);
        if (n > sizeof(tmp)) {
            n = sizeof(tmp);
        }
        if (n > 0) {
            uart_ll_read_rxfifo(uart0, tmp, n);
            for (uint32_t i = 0; i < n; i++) {
                cli_feed(&uart_port, (char)tmp[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CLI_POLL_MS));
    }
}

// ================= 初始化 =================

esp_err_t serial_cli_init(void)
{
    if (xTaskCreate(cli_task, "serial_cli", 8192, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
