/*
 * user_wifi.c
 */

#include "user_interface.h"
#include "osapi.h"
#include "espconn.h"
#include "os_type.h"
#include "mem.h"

#include "user_smartconfig.h"
#include "user_wifi.h"

//***********************************************************************/

// 如果不需要debug，则注释
#define DEBUG	1

// 每隔2s检查一次wifi状态
#define WIFI_CHECK_TIMER_INTERVAL	(2*1000)
// 每隔200ms发生一次wifi led事件
#define WIFI_LED_INTERVAL	200
// smartconfig 上电后等待时间
#define SMARTCONFIG_WAIT_TIME	(20*1000)

//***********************************************************************/
// debug
#define PR	os_printf

#ifdef DEBUG
#define debug(fmt, args...) PR(fmt, ##args)
#define debugX(level, fmt, args...) if(DEBUG>=level) PR(fmt, ##args);
#else
#define debug(fmt, args...)
#define debugX(level, fmt, args...)
#endif	/* DEBUG */

//***********************************************************************/
// define
#define GPIO_HIGH(x)	GPIO_OUTPUT_SET(x, 1)
#define GPIO_LOW(x)		GPIO_OUTPUT_SET(x, 0)
// GPIO reverse
#define GPIO_REV(x)		GPIO_OUTPUT_SET(x, (1-GPIO_INPUT_GET(x)))

#define IS_RUNNING		1
#define IS_STOP			0
//***********************************************************************/
// gloabl variable
static os_timer_t g_wifi_check_timer;
static os_timer_t g_wifi_led_timer;

static struct ip_info g_ipConfig;
static os_timer_t g_wifi_connect_timer;
static os_timer_t g_smartconig_led_timer;

static u8 g_smartconfig_running_flag = IS_STOP;
//***********************************************************************/

u32 ICACHE_FLASH_ATTR
get_station_ip(void) {
	return g_ipConfig.ip.addr;
}

/*************************************************************/

static void ICACHE_FLASH_ATTR
wifi_led_timer_cb(void *arg) {
	//wifi_get_ip_info(STATION_IF, &g_ipConfig);
	u8 status = wifi_station_get_connect_status();

	// 如果wifi连接成功，保持led常亮（低电平）
	// 否则，led闪烁
	// 闪烁间隔由 WIFI_LED_INTERVAL 确定
	if (status == STATION_GOT_IP) {
		GPIO_LOW(WIFI_STATUS_LED_PIN);
	} else {
		GPIO_REV(WIFI_STATUS_LED_PIN);
	}
}

void ICACHE_FLASH_ATTR
wifi_status_led_init(void) {
	// 使用定时器控制led显示wifi状态
	debug("[INFO] WiFi_LED_STATUS_TIMER_ENABLE\r\n");
	os_timer_disarm(&g_wifi_connect_timer);
	os_timer_setfn(&g_wifi_connect_timer, (os_timer_func_t *) wifi_led_timer_cb,
	NULL);
	os_timer_arm(&g_wifi_connect_timer, WIFI_LED_INTERVAL, 1);

}

/*************************************************************/
// smartconfig led config
/*
 * function: smartconfig_led_timer_cb
 */
static void ICACHE_FLASH_ATTR
smartconfig_led_timer_cb(void *arg) {
	GPIO_REV(WIFI_STATUS_LED_PIN);		// led闪烁
}

/*
 * function: user_smartconfig_led_timer_init
 * description: smartconfig闪烁led事件，间隔1000ms
 */
void ICACHE_FLASH_ATTR
user_smartconfig_led_timer_init(void) {
	os_timer_disarm(&g_smartconig_led_timer);
	os_timer_setfn(&g_smartconig_led_timer,
			(os_timer_func_t *) smartconfig_led_timer_cb, NULL);
	os_timer_arm(&g_smartconig_led_timer, 1000, 1);
}

/*
 * function: user_smartconfig_led_timer_stop
 * description: 停止smartconfig led闪烁ֹ
 */
void ICACHE_FLASH_ATTR
user_smartconfig_led_timer_stop(void) {
	os_timer_disarm(&g_smartconig_led_timer);
}

/*
 * function: user_smartconfig_init
 */
static void ICACHE_FLASH_ATTR
user_smartconfig_init(void) {

	//esptouch_set_timeout(30);
	//SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS
	smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS);
	smartconfig_start(smartconfig_done);
	g_smartconfig_running_flag = IS_RUNNING;
	GPIO_HIGH(WIFI_STATUS_LED_PIN);
	debug("[INFO] smartconfig start!\r\n");
}

/*************************************************************/
// wifi定时检查
/*
 * function: wifi_check_timer_cb
 * description: wiif检查回调函数
 *              wiif连接成功时每 2s 检查一次
 *              wiif断开时每 500ms 检查一次
 */
static void ICACHE_FLASH_ATTR
wifi_check_timer_cb(void) {
	static u8 from_disconnect_to_connect = 1;
	u8 status = wifi_station_get_connect_status();

	if (STATION_GOT_IP == status) {
		wifi_check_init(WIFI_CHECK_TIMER_INTERVAL);

		// 从wifi断开连接状态到连接成功状态
		if (1 == from_disconnect_to_connect) {
			from_disconnect_to_connect = 0;
			debug("[INFO] wifi connected!\r\n");
			// TODO:
			// wifi连接成功后
		}

	} else {	// wifi断开
		wifi_check_init(500);
		from_disconnect_to_connect = 1;
	}
}

/*
 * function: wifi_check_init
 * parameter: u16 interval - 定时回调时间
 * description: wifi检查初始化״̬
 */
void ICACHE_FLASH_ATTR
wifi_check_init(u16 interval) {
	wifi_station_set_reconnect_policy(TRUE);

	os_timer_disarm(&g_wifi_check_timer);
	os_timer_setfn(&g_wifi_check_timer, (os_timer_func_t *) wifi_check_timer_cb,
	NULL);
	os_timer_arm(&g_wifi_check_timer, interval, 0);
	//debug("[INFO] init Wi-Fi check!\r\n");
}

/*************************************************************/

/*
 * function: wifi_connect_timer_cb
 */
static void ICACHE_FLASH_ATTR
wifi_connect_timer_cb(void *arg) {

	os_timer_disarm(&g_wifi_connect_timer);	// stop g_wifi_connect_timer

	wifi_get_ip_info(STATION_IF, &g_ipConfig);
	u8 wifi_status = wifi_station_get_connect_status();

	user_smartconfig_led_timer_stop();	// 停止smartconfig控制led
	if (IS_RUNNING == g_smartconfig_running_flag) {
		debug("[INFO] smartconfig stop!\r\n");
		smartconfig_stop();			// 无论smartconfig是否成功都要释放
		g_smartconfig_running_flag = IS_STOP;
	}

	// 检查wifi是否连接
	if (STATION_GOT_IP == wifi_status && g_ipConfig.ip.addr != 0) {
		debug("[INFO] Wi-Fi connected from smartconfig!\r\n");
	} else {
		debug("[INFO] Wi-Fi connect fail from smartconfig!\r\n");
		wifi_station_disconnect();
		wifi_station_connect();		// 尝试重连
	}

	wifi_check_init(WIFI_CHECK_TIMER_INTERVAL);		// wifi check
	wifi_status_led_init();	// wifi led

	wifi_station_set_reconnect_policy(TRUE);	//
	wifi_station_set_auto_connect(TRUE);		// auto connect wifi
}

/*
 * function: wifi_connect_timer_init
 * description:
 */
static void ICACHE_FLASH_ATTR
wifi_connect_timer_init(void) {
	os_timer_disarm(&g_wifi_connect_timer);
	os_timer_setfn(&g_wifi_connect_timer,
			(os_timer_func_t *) wifi_connect_timer_cb, NULL);
	os_timer_arm(&g_wifi_connect_timer, SMARTCONFIG_WAIT_TIME, 0);
}

/*************************************************************/

/*
 * function: user_set_station_config
 * parameter: u8* ssid - WiFi SSID
 *            u8 password - WiFi password
 * return: void
 */
void ICACHE_FLASH_ATTR
user_set_station_config(u8* ssid, u8* password) {
	struct station_config stationConf;
	stationConf.bssid_set = 0;		//need not check MAC address of AP
	os_memcpy(&stationConf.ssid, ssid, 32);
	os_memcpy(&stationConf.password, password, 64);
	wifi_station_set_config(&stationConf);
}

/*
 * function: wifi_connect
 */
void ICACHE_FLASH_ATTR
wifi_connect(void) {
	//wifi_set_opmode(STATION_MODE);		// set wifi mode
	wifi_station_disconnect();

#ifdef WIFI_SSID_ENABLE
	user_set_station_config(WIFI_SSID, WIFI_PASS);
	wifi_station_connect();

	wifi_check_init(WIFI_CHECK_TIMER_INTERVAL);		// wifi check
	wifi_status_led_init();// wifi led

	/* WIFI_SSID_ENABLE */
#elif defined(SMARTCONFIG_ENABLE)
	debug("[INFO] SMARTCONFIG_ENABLE\r\n");
	wifi_station_set_reconnect_policy(FALSE);
	wifi_station_set_auto_connect(FALSE);

	user_smartconfig_init();
	wifi_connect_timer_init();
#endif	/* SMARTCONFIG_ENABLE */
}
