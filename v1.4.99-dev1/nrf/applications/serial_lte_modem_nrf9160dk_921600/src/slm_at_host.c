/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdio.h>
#include <ctype.h>
#include <logging/log.h>
#include <drivers/uart.h>
#include <string.h>
#include <init.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>
#include <power/reboot.h>

LOG_MODULE_REGISTER(at_host, CONFIG_SLM_LOG_LEVEL);

#include "slm_util.h"
#include "slm_at_host.h"
#include "slm_at_tcp_proxy.h"
#include "slm_at_udp_proxy.h"
#include "slm_at_tcpip.h"
#if defined(CONFIG_SLM_NATIVE_TLS)
#include "slm_at_cmng.h"
#endif
#include "slm_at_icmp.h"
#include "slm_at_fota.h"
#if defined(CONFIG_SLM_GPS)
#include "slm_at_gps.h"
#endif
#if defined(CONFIG_SLM_FTPC)
#include "slm_at_ftp.h"
#endif
#if defined(CONFIG_SLM_MQTTC)
#include "slm_at_mqtt.h"
#endif
#if defined(CONFIG_SLM_HTTPC)
#include "slm_at_httpc.h"
#endif

#define OK_STR		"OK\r\n"
#define ERROR_STR	"ERROR\r\n"
#define FATAL_STR	"FATAL ERROR\r\n"
#define SLM_SYNC_STR	"Ready\r\n"

#define SLM_VERSION	"#XSLMVER: 1.5\r\n"
#define AT_CMD_SLMVER	"AT#XSLMVER"
#define AT_CMD_SLEEP	"AT#XSLEEP"
#define AT_CMD_RESET	"AT#XRESET"
#define AT_CMD_CLAC	"AT#XCLAC"
#define AT_CMD_SLMUART	"AT#XSLMUART"

#define SLM_UART_BAUDRATE                                           \
	"#XSLMUART: (1200, 2400, 4800, 9600, 14400, 19200, 38400, " \
	"57600, 115200, 230400, 460800, 921600, 1000000)\r\n"

#define AT_MAX_CMD_LEN	CONFIG_AT_CMD_RESPONSE_MAX_LEN
#define UART_RX_BUF_NUM	2
#define UART_RX_LEN	256
#define UART_RX_TIMEOUT 1

/** @brief Termination Modes. */
enum term_modes {
	MODE_NULL_TERM, /**< Null Termination */
	MODE_CR,        /**< CR Termination */
	MODE_LF,        /**< LF Termination */
	MODE_CR_LF,     /**< CR+LF Termination */
	MODE_COUNT      /* Counter of term_modes */
};

/**@brief Shutdown modes. */
enum shutdown_modes {
	SHUTDOWN_MODE_IDLE,
	SHUTDOWN_MODE_SLEEP,
	SHUTDOWN_MODE_INVALID
};

static enum term_modes term_mode;
static const struct device *uart_dev;
static uint8_t at_buf[AT_MAX_CMD_LEN];
static size_t at_buf_len;
static struct k_work cmd_send_work;
static const char termination[3] = { '\0', '\r', '\n' };

static uint8_t uart_rx_buf[UART_RX_BUF_NUM][UART_RX_LEN];
static uint8_t *next_buf = uart_rx_buf[1];
static uint8_t *uart_tx_buf;

static K_SEM_DEFINE(tx_done, 0, 1);

/* global functions defined in different files */
void enter_idle(void);
void enter_sleep(bool wake_up);

/* global variable defined in different files */
extern struct at_param_list at_param_list;

/* forward declaration */
void slm_at_host_uninit(void);

void rsp_send(const uint8_t *str, size_t len)
{
	int ret;

	k_sem_take(&tx_done, K_FOREVER);

	uart_tx_buf = k_malloc(len);
	if (uart_tx_buf == NULL) {
		LOG_WRN("No ram buffer");
		k_sem_give(&tx_done);
		return;
	}

	LOG_HEXDUMP_DBG(str, len, "TX");

	memcpy(uart_tx_buf, str, len);
	ret = uart_tx(uart_dev, uart_tx_buf, len, SYS_FOREVER_MS);
	if (ret) {
		LOG_WRN("uart_tx failed: %d", ret);
		k_free(uart_tx_buf);
		k_sem_give(&tx_done);
	}
}

static int set_uart_baudrate(uint32_t baudrate)
{
	int err = -EINVAL;
	struct uart_config cfg;

	LOG_DBG("Set uart baudrate to: %d", baudrate);

	err = uart_config_get(uart_dev, &cfg);
	if (err != 0) {
		LOG_ERR("uart_config_get: %d", err);
		return err;
	}
	cfg.baudrate = baudrate;
	err = uart_configure(uart_dev, &cfg);
	if (err != 0) {
		LOG_ERR("uart_configure: %d", err);
		return err;
	}

	return err;
}

static int get_uart_baudrate(void)
{
	int err = -EINVAL;
	struct uart_config cfg;

	err = uart_config_get(uart_dev, &cfg);
	if (err) {
		LOG_ERR("uart_config_get: %d", err);
		return err;
	}
	return (int)cfg.baudrate;
}

static void response_handler(void *context, const char *response)
{
	int len = strlen(response);

	ARG_UNUSED(context);

	/* Forward the data over UART */
	if (len > 0) {
		rsp_send(response, len);
	}
}

static void handle_at_clac(void)
{
	rsp_send(AT_CMD_SLMVER, sizeof(AT_CMD_SLMVER) - 1);
	rsp_send("\r\n", 2);
	rsp_send(AT_CMD_SLMUART, sizeof(AT_CMD_SLMUART) - 1);
	rsp_send("\r\n", 2);
	rsp_send(AT_CMD_SLEEP, sizeof(AT_CMD_SLEEP) - 1);
	rsp_send("\r\n", 2);
	rsp_send(AT_CMD_RESET, sizeof(AT_CMD_RESET) - 1);
	rsp_send("\r\n", 2);
	rsp_send(AT_CMD_CLAC, sizeof(AT_CMD_CLAC) - 1);
	rsp_send("\r\n", 2);
	slm_at_tcp_proxy_clac();
	slm_at_udp_proxy_clac();
	slm_at_tcpip_clac();
#if defined(CONFIG_SLM_NATIVE_TLS)
	slm_at_cmng_clac();
#endif
	slm_at_icmp_clac();
	slm_at_fota_clac();
#if defined(CONFIG_SLM_GPS)
	slm_at_gps_clac();
#endif
#if defined(CONFIG_SLM_FTPC)
	slm_at_ftp_clac();
#endif
#if defined(CONFIG_SLM_MQTTC)
	slm_at_mqtt_clac();
#endif
#if defined(CONFIG_SLM_HTTPC)
	slm_at_httpc_clac();
#endif
}

static int handle_at_sleep(const char *at_cmd, enum shutdown_modes *mode)
{
	int ret = -EINVAL;
	enum at_cmd_type type;
	uint16_t shutdown_mode;

	ret = at_parser_params_from_str(at_cmd, NULL, &at_param_list);
	if (ret < 0) {
		LOG_ERR("Failed to parse AT command %d", ret);
		return -EINVAL;
	}

	type = at_parser_cmd_type_get(at_cmd);
	if (type == AT_CMD_TYPE_SET_COMMAND) {
		shutdown_mode = SHUTDOWN_MODE_IDLE;
		if (at_params_valid_count_get(&at_param_list) > 1) {
			ret = at_params_short_get(&at_param_list, 1,
					&shutdown_mode);
			if (ret < 0) {
				LOG_ERR("AT parameter error");
				return -EINVAL;
			}
		}
		if (shutdown_mode == SHUTDOWN_MODE_IDLE) {
			slm_at_host_uninit();
			enter_idle();
			*mode = SHUTDOWN_MODE_IDLE;
			ret = 0; /*Will send no "OK"*/
		} else if (shutdown_mode == SHUTDOWN_MODE_SLEEP) {
			slm_at_host_uninit();
			enter_sleep(true);
			ret = 0; /* Cannot reach here */
		} else {
			LOG_ERR("AT parameter error");
			ret = -EINVAL;
		}
	}

	if (type == AT_CMD_TYPE_TEST_COMMAND) {
		char buf[64];

		sprintf(buf, "#XSLEEP: (%d, %d)\r\n", SHUTDOWN_MODE_IDLE,
			SHUTDOWN_MODE_SLEEP);
		rsp_send(buf, strlen(buf));
		ret = 0;
	}

	return ret;
}

static int handle_at_slmuart(const char *at_cmd, uint32_t *baudrate)
{
	int ret = -EINVAL;
	enum at_cmd_type type;

	ret = at_parser_params_from_str(at_cmd, NULL, &at_param_list);
	if (ret < 0) {
		LOG_ERR("Failed to parse AT command %d", ret);
		return -EINVAL;
	}

	type = at_parser_cmd_type_get(at_cmd);
	if (type == AT_CMD_TYPE_SET_COMMAND) {
		if (at_params_valid_count_get(&at_param_list) > 1) {
			ret = at_params_int_get(&at_param_list, 1,
					baudrate);
			if (ret < 0) {
				LOG_ERR("AT parameter error");
				return -EINVAL;
			}
		}
		switch (*baudrate) {
		case 1200:
		case 2400:
		case 4800:
		case 9600:
		case 14400:
		case 19200:
		case 38400:
		case 57600:
		case 115200:
		case 230400:
		case 460800:
		case 921600:
		case 1000000:
			ret = 0;
			break;
		default:
			LOG_ERR("Invalid uart baud rate provided.");
			return -EINVAL;
		}
	}

	if (type == AT_CMD_TYPE_READ_COMMAND) {
		char buf[32];

		sprintf(buf, "#SLMUART: %d\r\n", get_uart_baudrate());
		rsp_send(buf, strlen(buf));
		ret = 0;
	}

	if (type == AT_CMD_TYPE_TEST_COMMAND) {
		char buf[] = SLM_UART_BAUDRATE;

		rsp_send(buf, sizeof(buf));
		ret = 0;
	}

	return ret;
}

static void cmd_send(struct k_work *work)
{
	size_t chars;
	char str[24];
	static char buf[AT_MAX_CMD_LEN];
	enum at_cmd_state state;
	int err;

	ARG_UNUSED(work);

	/* Make sure the string is 0-terminated */
	at_buf[MIN(at_buf_len, AT_MAX_CMD_LEN - 1)] = 0;

	LOG_HEXDUMP_DBG(at_buf, at_buf_len, "RX");

	if (slm_util_cmd_casecmp(at_buf, AT_CMD_SLMVER)) {
		rsp_send(SLM_VERSION, sizeof(SLM_VERSION) - 1);
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	}

	if (slm_util_cmd_casecmp(at_buf, AT_CMD_SLMUART)) {
		uint32_t baudrate;

		err = handle_at_slmuart(at_buf, &baudrate);
		if (err != 0) {
			rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
			goto done;
		} else {
			rsp_send(OK_STR, sizeof(OK_STR) - 1);
			k_sleep(K_MSEC(50));
			set_uart_baudrate(baudrate);
			goto done;
		}
	}

	if (slm_util_cmd_casecmp(at_buf, AT_CMD_RESET)) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		k_sleep(K_MSEC(50));
		slm_at_host_uninit();
		enter_sleep(false);
		sys_reboot(SYS_REBOOT_COLD);
	}

	if (slm_util_cmd_casecmp(at_buf, AT_CMD_CLAC)) {
		handle_at_clac();
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	}

	if (slm_util_cmd_casecmp(at_buf, AT_CMD_SLEEP)) {
		enum shutdown_modes mode = SHUTDOWN_MODE_INVALID;

		err = handle_at_sleep(at_buf, &mode);
		if (err) {
			rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
			goto done;
		} else {
			if (mode == SHUTDOWN_MODE_INVALID) {
				/*Test command*/
				rsp_send(OK_STR, sizeof(OK_STR) - 1);
				goto done;
			} else {
				/*Entered IDLE*/
				return;
			}
		}
	}

	err = slm_at_tcp_proxy_parse(at_buf, at_buf_len);
	if (err > 0) {
		goto done;
	} else if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}

	err = slm_at_udp_proxy_parse(at_buf, at_buf_len);
	if (err > 0) {
		goto done;
	} else if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}

	err = slm_at_tcpip_parse(at_buf);
	if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}

#if defined(CONFIG_SLM_NATIVE_TLS)
	err = slm_at_cmng_parse(at_buf);
	if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}
#endif

	err = slm_at_icmp_parse(at_buf);
	if (err == 0) {
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}

	err = slm_at_fota_parse(at_buf);
	if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}

#if defined(CONFIG_SLM_GPS)
	err = slm_at_gps_parse(at_buf);
	if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}
#endif

#if defined(CONFIG_SLM_FTPC)
	err = slm_at_ftp_parse(at_buf);
	if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}
#endif

#if defined(CONFIG_SLM_MQTTC)
	err = slm_at_mqtt_parse(at_buf);
	if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}
#endif

#if defined(CONFIG_SLM_HTTPC)
	err = slm_at_httpc_parse(at_buf, at_buf_len);
	if (err == 0) {
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		goto done;
	} else if (err != -ENOENT) {
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		goto done;
	}
#endif

	/* Send to modem */
	err = at_cmd_write(at_buf, buf, AT_MAX_CMD_LEN, &state);
	if (err < 0) {
		LOG_ERR("AT command error: %d", err);
		state = AT_CMD_ERROR;
	}

	switch (state) {
	case AT_CMD_OK:
		rsp_send(buf, strlen(buf));
		rsp_send(OK_STR, sizeof(OK_STR) - 1);
		break;
	case AT_CMD_ERROR:
		rsp_send(ERROR_STR, sizeof(ERROR_STR) - 1);
		break;
	case AT_CMD_ERROR_CMS:
		chars = sprintf(str, "+CMS: %d\r\n", err);
		rsp_send(str, ++chars);
		break;
	case AT_CMD_ERROR_CME:
		chars = sprintf(str, "+CME: %d\r\n", err);
		rsp_send(str, ++chars);
		break;
	default:
		break;
	}

done:
	err = uart_rx_enable(uart_dev, uart_rx_buf[0],
				sizeof(uart_rx_buf[0]), UART_RX_TIMEOUT);
	if (err) {
		LOG_ERR("UART RX failed: %d", err);
		rsp_send(FATAL_STR, sizeof(FATAL_STR) - 1);
	}
}

static void uart_rx_handler(uint8_t character)
{
	static bool inside_quotes;
	static size_t cmd_len;
	size_t pos;

	cmd_len += 1;
	pos = cmd_len - 1;

	/* Handle special characters. */
	switch (character) {
	case 0x08: /* Backspace. */
		/* Fall through. */
	case 0x7F: /* DEL character */
		pos = pos ? pos - 1 : 0;
		at_buf[pos] = 0;
		cmd_len = cmd_len <= 1 ? 0 : cmd_len - 2;
		break;
	case '"':
		inside_quotes = !inside_quotes;
		 /* Fall through. */
	default:
		/* Detect AT command buffer overflow or zero length */
		if (cmd_len > AT_MAX_CMD_LEN) {
			LOG_ERR("Buffer overflow, dropping '%c'\n", character);
			cmd_len = AT_MAX_CMD_LEN;
			return;
		} else if (cmd_len < 1) {
			LOG_ERR("Invalid AT command length: %d", cmd_len);
			cmd_len = 0;
			return;
		}

		at_buf[pos] = character;
		break;
	}

	if (inside_quotes) {
		return;
	}

	/* Check if the character marks line termination. */
	switch (term_mode) {
	case MODE_NULL_TERM:
		goto send;
	case MODE_CR:
		if (character == termination[term_mode]) {
			cmd_len--;
			goto send;
		}
		break;
	case MODE_LF:
		if ((at_buf[pos - 1]) &&
			character == termination[term_mode]) {
			cmd_len--;
			goto send;
		}
		break;
	case MODE_CR_LF:
		if ((at_buf[pos - 1] == '\r') && (character == '\n')) {
			cmd_len -= 2;
			goto send;
		}
		break;
	default:
		LOG_ERR("Invalid termination mode: %d", term_mode);
		break;
	}

	return;
send:
	uart_rx_disable(uart_dev);
	k_work_submit(&cmd_send_work);
	at_buf_len = cmd_len;
	cmd_len = 0;
}

static void uart_callback(const struct device *dev, struct uart_event *evt,
			  void *user_data)
{
	ARG_UNUSED(dev);

	int err;
	static uint16_t pos;

	ARG_UNUSED(user_data);

	switch (evt->type) {
	case UART_TX_DONE:
		k_free(uart_tx_buf);
		k_sem_give(&tx_done);
		break;
	case UART_TX_ABORTED:
		k_free(uart_tx_buf);
		k_sem_give(&tx_done);
		LOG_INF("TX_ABORTED");
		break;
	case UART_RX_RDY:
		for (int i = pos; i < (pos + evt->data.rx.len); i++) {
			uart_rx_handler(evt->data.rx.buf[i]);
		}
		pos += evt->data.rx.len;
		break;
	case UART_RX_BUF_REQUEST:
		pos = 0;
		err = uart_rx_buf_rsp(uart_dev, next_buf,
					sizeof(uart_rx_buf[0]));
		if (err) {
			LOG_WRN("UART RX buf rsp: %d", err);
		}
		break;
	case UART_RX_BUF_RELEASED:
		next_buf = evt->data.rx_buf.buf;
		break;
	case UART_RX_STOPPED:
		LOG_WRN("RX_STOPPED (%d)", evt->data.rx_stop.reason);
		break;
	case UART_RX_DISABLED:
		LOG_DBG("RX_DISABLED");
		break;
	default:
		break;
	}
}

int slm_at_host_init(void)
{
	int err;
	uint32_t start_time;

	/* Initialize the UART module */
#if defined(CONFIG_SLM_CONNECT_UART_0)
	uart_dev = device_get_binding(DT_LABEL(DT_NODELABEL(uart0)));
#elif defined(CONFIG_SLM_CONNECT_UART_2)
	uart_dev = device_get_binding(DT_LABEL(DT_NODELABEL(uart2)));
#else
	LOG_ERR("Unsupported UART instance");
	return -EINVAL;
#endif
	if (uart_dev == NULL) {
		LOG_ERR("Cannot bind UART device\n");
		return -EINVAL;
	}
	/* Wait for the UART line to become valid */
	start_time = k_uptime_get_32();
	do {
		err = uart_err_check(uart_dev);
		if (err) {
			if (k_uptime_get_32() - start_time > 500) {
				LOG_ERR("UART check failed: %d. "
					"UART initialization timed out.", err);
				return -EIO;
			}
		}
	} while (err);
	/* Register async handling callback */
	err = uart_callback_set(uart_dev, uart_callback, NULL);
	if (err) {
		LOG_ERR("Cannot set callback: %d", err);
		return -EFAULT;
	}
	/* Power on UART module */
	device_set_power_state(uart_dev, DEVICE_PM_ACTIVE_STATE,
				NULL, NULL);
	term_mode = CONFIG_SLM_AT_HOST_TERMINATION;
	err = uart_rx_enable(uart_dev, uart_rx_buf[0],
				sizeof(uart_rx_buf[0]), UART_RX_TIMEOUT);
	if (err) {
		LOG_ERR("Cannot enable rx: %d", err);
		return -EFAULT;
	}

	err = at_notif_register_handler(NULL, response_handler);
	if (err) {
		LOG_ERR("Can't register handler err=%d", err);
		return err;
	}

	err = slm_at_tcp_proxy_init();
	if (err) {
		LOG_ERR("TCP Server could not be initialized: %d", err);
		return -EFAULT;
	}
	err = slm_at_udp_proxy_init();
	if (err) {
		LOG_ERR("UDP Server could not be initialized: %d", err);
		return -EFAULT;
	}
	err = slm_at_tcpip_init();
	if (err) {
		LOG_ERR("TCPIP could not be initialized: %d", err);
		return -EFAULT;
	}
#if defined(CONFIG_SLM_NATIVE_TLS)
	err = slm_at_cmng_init();
	if (err) {
		LOG_ERR("TLS could not be initialized: %d", err);
		return -EFAULT;
	}
#endif
	err = slm_at_icmp_init();
	if (err) {
		LOG_ERR("ICMP could not be initialized: %d", err);
		return -EFAULT;
	}
	err = slm_at_fota_init();
	if (err) {
		LOG_ERR("FOTA could not be initialized: %d", err);
		return -EFAULT;
	}
#if defined(CONFIG_SLM_GPS)
	err = slm_at_gps_init();
	if (err) {
		LOG_ERR("GPS could not be initialized: %d", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SLM_FTPC)
	err = slm_at_ftp_init();
	if (err) {
		LOG_ERR("FTP could not be initialized: %d", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SLM_MQTTC)
	err = slm_at_mqtt_init();
	if (err) {
		LOG_ERR("MQTT could not be initialized: %d", err);
		return -EFAULT;
	}
#endif
#if defined(CONFIG_SLM_HTTPC)
	err = slm_at_httpc_init();
	if (err) {
		LOG_ERR("HTTP could not be initialized: %d", err);
		return -EFAULT;
	}
#endif
	k_work_init(&cmd_send_work, cmd_send);
	k_sem_give(&tx_done);
	rsp_send(SLM_SYNC_STR, sizeof(SLM_SYNC_STR)-1);

	LOG_DBG("at_host init done");
	return err;
}

void slm_at_host_uninit(void)
{
	int err;

	err = slm_at_tcp_proxy_uninit();
	if (err) {
		LOG_WRN("TCP Server could not be uninitialized: %d", err);
	}
	err = slm_at_udp_proxy_uninit();
	if (err) {
		LOG_WRN("UDP Server could not be uninitialized: %d", err);
	}
	err = slm_at_tcpip_uninit();
	if (err) {
		LOG_WRN("TCPIP could not be uninitialized: %d", err);
	}
#if defined(CONFIG_SLM_NATIVE_TLS)
	err = slm_at_cmng_uninit();
	if (err) {
		LOG_WRN("TLS could not be uninitialized: %d", err);
	}
#endif
	err = slm_at_icmp_uninit();
	if (err) {
		LOG_WRN("ICMP could not be uninitialized: %d", err);
	}
	err = slm_at_fota_uninit();
	if (err) {
		LOG_WRN("FOTA could not be uninitialized: %d", err);
	}
#if defined(CONFIG_SLM_GPS)
	err = slm_at_gps_uninit();
	if (err) {
		LOG_WRN("GPS could not be uninitialized: %d", err);
	}
#endif
#if defined(CONFIG_SLM_FTPC)
	err = slm_at_ftp_uninit();
	if (err) {
		LOG_WRN("FTP could not be uninitialized: %d", err);
	}
#endif
#if defined(CONFIG_SLM_MQTTC)
	err = slm_at_mqtt_uninit();
	if (err) {
		LOG_WRN("MQTT could not be uninitialized: %d", err);
	}
#endif
#if defined(CONFIG_SLM_HTTPC)
	err = slm_at_httpc_uninit();
	if (err) {
		LOG_WRN("HTTP could not be uninitialized: %d", err);
	}

	err = at_notif_deregister_handler(NULL, response_handler);
	if (err) {
		LOG_WRN("Can't deregister handler: %d", err);
	}
#endif
	/* Power off UART module */
	uart_rx_disable(uart_dev);
	k_sleep(K_MSEC(100));
	err = device_set_power_state(uart_dev, DEVICE_PM_OFF_STATE,
				NULL, NULL);
	if (err) {
		LOG_WRN("Can't power off uart: %d", err);
	}

	LOG_DBG("at_host uninit done");
}
