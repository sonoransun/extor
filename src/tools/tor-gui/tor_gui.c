/* tor_gui.c -- Main entry point for tor-gui.
 *
 * tor-gui is a standalone local web UI for monitoring and controlling
 * a running Tor instance via its control port.  It embeds an HTTP
 * server (Mongoose) that serves a static web frontend and proxies
 * Tor control protocol commands through REST and WebSocket APIs.
 *
 * Usage:
 *   tor-gui [--control-port HOST:PORT] [--control-password PASS]
 *           [--control-cookie PATH] [--listen ADDR:PORT]
 *           [--webroot PATH] [--no-browser] [--log-level LEVEL]
 *           [--help]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "tg_control.h"
#include "tg_http.h"
#include "tg_api.h"
#include "tg_events.h"
#include "tg_util.h"

/* Default configuration */
#define DEFAULT_CONTROL_HOST "127.0.0.1"
#define DEFAULT_CONTROL_PORT 9051
#define DEFAULT_LISTEN_ADDR  "127.0.0.1:8073"

static volatile int s_running = 1;

static void
signal_handler(int sig)
{
  (void)sig;
  s_running = 0;
}

/** Print usage information and exit. */
static void
print_usage(const char *prog)
{
  fprintf(stderr,
    "Usage: %s [OPTIONS]\n"
    "\n"
    "A local web GUI for monitoring and controlling Tor.\n"
    "\n"
    "Options:\n"
    "  --control-port HOST:PORT   Tor control port "
    "(default: 127.0.0.1:9051)\n"
    "  --control-password PASS    Control port password\n"
    "  --control-cookie PATH      Path to control_auth_cookie file\n"
    "  --listen ADDR:PORT         HTTP listen address "
    "(default: 127.0.0.1:8073)\n"
    "  --webroot PATH             Path to static web files directory\n"
    "  --no-browser               Don't auto-open browser on startup\n"
    "  --log-level LEVEL          Log level: error, warn, info, debug "
    "(default: info)\n"
    "  --help                     Show this help message\n"
    "\n"
    "The GUI will be available at http://ADDR:PORT/?token=TOKEN\n",
    prog);
}

/** Parse HOST:PORT string.  Writes host into `host_out` (max
 *  `host_len`) and returns the port number.  Returns -1 on error. */
static int
parse_host_port(const char *str, char *host_out, size_t host_len)
{
  const char *colon;
  size_t hlen;
  int port;

  colon = strrchr(str, ':');
  if (!colon || colon == str) {
    /* No colon or colon at start -- treat whole thing as host,
     * return default port */
    tg_strlcpy(host_out, str, host_len);
    return -1;
  }

  hlen = (size_t)(colon - str);
  if (hlen >= host_len)
    hlen = host_len - 1;
  memcpy(host_out, str, hlen);
  host_out[hlen] = '\0';

  port = atoi(colon + 1);
  if (port <= 0 || port > 65535)
    return -1;

  return port;
}

/** Try to auto-detect the webroot by checking common relative paths. */
static int
detect_webroot(char *out, size_t outlen)
{
  static const char *candidates[] = {
    "./static",
    "../static",
    "src/tools/tor-gui/static",
    "../src/tools/tor-gui/static",
    NULL
  };
  int i;

  for (i = 0; candidates[i]; i++) {
    if (tg_dir_exists(candidates[i])) {
      tg_strlcpy(out, candidates[i], outlen);
      tg_log(TG_LOG_DEBUG, "Auto-detected webroot: %s", out);
      return 0;
    }
  }

  return -1;
}

int
main(int argc, char **argv)
{
  tg_ctrl_t ctrl;
  tg_http_t http;
  tg_events_t events;
  tg_http_config_t http_config;

  char ctrl_host[256];
  int ctrl_port = DEFAULT_CONTROL_PORT;
  const char *ctrl_password = NULL;
  const char *ctrl_cookie = NULL;
  int no_browser = 0;
  int i;

  /* Initialize defaults */
  tg_strlcpy(ctrl_host, DEFAULT_CONTROL_HOST, sizeof(ctrl_host));
  memset(&http_config, 0, sizeof(http_config));
  tg_strlcpy(http_config.listen_addr, DEFAULT_LISTEN_ADDR,
              sizeof(http_config.listen_addr));

  /* Parse command-line arguments */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--control-port") == 0 && i + 1 < argc) {
      int p = parse_host_port(argv[++i], ctrl_host, sizeof(ctrl_host));
      if (p > 0)
        ctrl_port = p;
    } else if (strcmp(argv[i], "--control-password") == 0 &&
               i + 1 < argc) {
      ctrl_password = argv[++i];
    } else if (strcmp(argv[i], "--control-cookie") == 0 &&
               i + 1 < argc) {
      ctrl_cookie = argv[++i];
    } else if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
      tg_strlcpy(http_config.listen_addr, argv[++i],
                  sizeof(http_config.listen_addr));
    } else if (strcmp(argv[i], "--webroot") == 0 && i + 1 < argc) {
      tg_strlcpy(http_config.webroot, argv[++i],
                  sizeof(http_config.webroot));
    } else if (strcmp(argv[i], "--no-browser") == 0) {
      no_browser = 1;
    } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
      const char *level = argv[++i];
      if (strcmp(level, "error") == 0)
        tg_log_set_level(TG_LOG_ERROR);
      else if (strcmp(level, "warn") == 0)
        tg_log_set_level(TG_LOG_WARN);
      else if (strcmp(level, "info") == 0)
        tg_log_set_level(TG_LOG_INFO);
      else if (strcmp(level, "debug") == 0)
        tg_log_set_level(TG_LOG_DEBUG);
      else {
        fprintf(stderr, "Unknown log level: %s\n", level);
        return 1;
      }
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  http_config.no_browser = no_browser;

  /* Auto-detect webroot if not explicitly set */
  if (http_config.webroot[0] == '\0') {
    if (detect_webroot(http_config.webroot,
                       sizeof(http_config.webroot)) != 0) {
      tg_log(TG_LOG_WARN, "Could not auto-detect webroot directory. "
             "Use --webroot to specify the path to static files.");
    }
  }

  /* Set up signal handlers */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  /* Platform initialization */
  tg_platform_init();

  /* Print startup banner */
  fprintf(stdout,
    "\n"
    "  tor-gui - Tor Control Port Web Interface\n"
    "  =========================================\n"
    "\n"
    "  Control port:  %s:%d\n"
    "  HTTP listen:   %s\n",
    ctrl_host, ctrl_port, http_config.listen_addr);
  if (http_config.webroot[0] != '\0')
    fprintf(stdout, "  Web root:      %s\n", http_config.webroot);
  fprintf(stdout, "\n");

  /* Connect to control port */
  tg_ctrl_init(&ctrl);
  if (tg_ctrl_connect(&ctrl, ctrl_host, ctrl_port) != 0) {
    fprintf(stderr,
      "Error: Cannot connect to Tor control port at %s:%d\n"
      "\n"
      "Make sure Tor is running and ControlPort is enabled.\n"
      "Add these lines to your torrc:\n"
      "  ControlPort 9051\n"
      "  CookieAuthentication 1\n",
      ctrl_host, ctrl_port);
    tg_ctrl_close(&ctrl);
    return 1;
  }

  /* Authenticate */
  if (tg_ctrl_authenticate(&ctrl, ctrl_password, ctrl_cookie) != 0) {
    fprintf(stderr,
      "Error: Authentication to Tor control port failed.\n"
      "\n"
      "Try one of:\n"
      "  - Enable CookieAuthentication in torrc\n"
      "  - Use --control-password with HashedControlPassword\n"
      "  - Use --control-cookie to specify cookie file path\n");
    tg_ctrl_close(&ctrl);
    return 1;
  }

  /* Start the async reader thread */
  if (tg_ctrl_start_reader(&ctrl) != 0) {
    fprintf(stderr, "Error: Failed to start control port reader\n");
    tg_ctrl_close(&ctrl);
    return 1;
  }

  /* Subscribe to events */
  if (tg_ctrl_subscribe(&ctrl,
      "CIRC STREAM ORCONN BW STATUS_CLIENT CONF_CHANGED") != 0) {
    tg_log(TG_LOG_WARN, "Failed to subscribe to some events");
  }

  /* Set up event forwarding to WebSocket clients */
  tg_events_init(&events, &http, &ctrl);
  tg_ctrl_set_event_cb(&ctrl, tg_events_on_ctrl_event, &events);
  http.events = &events;

  /* Initialize HTTP server */
  if (tg_http_init(&http, &ctrl, &http_config) != 0) {
    fprintf(stderr, "Error: Failed to start HTTP server on %s\n",
            http_config.listen_addr);
    tg_ctrl_close(&ctrl);
    return 1;
  }

  /* Print access information */
  fprintf(stdout,
    "  Access URL:    http://%s/?token=%s\n"
    "  Bearer token:  %s\n"
    "\n"
    "  Press Ctrl+C to stop.\n\n",
    http_config.listen_addr, http.config.bearer_token,
    http.config.bearer_token);
  fflush(stdout);

  /* Open browser */
  if (!no_browser) {
    char url[1024];
    snprintf(url, sizeof(url), "http://%s/?token=%s",
             http_config.listen_addr, http.config.bearer_token);
    tg_http_open_browser(url);
  }

  /* Main event loop */
  while (s_running) {
    /* Check for control port errors */
    if (tg_ctrl_get_state(&ctrl) == TG_CTRL_ERROR) {
      tg_log(TG_LOG_ERROR, "Control port connection lost");
      break;
    }

    /* Drain control port events and forward to WebSocket clients */
    tg_ctrl_drain_events(&ctrl);

    /* Process HTTP connections */
    tg_http_poll(&http, 100);
  }

  /* Shutdown */
  fprintf(stdout, "\nShutting down...\n");

  tg_http_free(&http);
  tg_ctrl_close(&ctrl);
  tg_platform_cleanup();

  fprintf(stdout, "Goodbye.\n");
  return 0;
}
