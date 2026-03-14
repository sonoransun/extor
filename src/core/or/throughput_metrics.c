/* Copyright (c) 2024, The Tor Project, Inc. */
/* See LICENSE for licensing information */

/**
 * @file throughput_metrics.c
 * @brief Rate-based throughput metrics, reported via SIGUSR1.
 *
 * Provides dump_throughput_metrics() which snapshots all existing global
 * counters (bytes, cells, connections, circuits), computes deltas from
 * the previous snapshot, and logs per-second rates via tor_log() with
 * LD_HEARTBEAT.  Output goes to whatever log targets are configured
 * (stdout, file, syslog).
 **/

#include "core/or/or.h"

#include "core/mainloop/connection.h"
#include "core/mainloop/cpuworker.h"
#include "core/mainloop/mainloop.h"
#include "core/or/circuitlist.h"
#include "core/or/command.h"
#include "core/or/relay.h"
#include "core/or/throughput_metrics.h"
#include "core/or/connection_st.h"

#include "feature/stats/rephist.h"

#include "lib/buf/buffers.h"
#include "lib/container/smartlist.h"
#include "lib/log/log.h"
#include "lib/net/socket.h"

/** Snapshot of all counters at a point in time. */
typedef struct throughput_snapshot_t {
  time_t timestamp;

  /* Byte counters (cumulative since process start) */
  uint64_t bytes_read;
  uint64_t bytes_written;

  /* Cell counters (cumulative) */
  uint64_t cells_relayed;
  uint64_t cells_delivered;
  uint64_t cells_processed;

  /* Instantaneous gauges */
  int n_circuits;
  int n_open_sockets;
  size_t buf_allocation;
  uint32_t pending_tasks;

  /* Per-type connection counts (from connection array) */
  int conn_count_or;
  int conn_count_exit;
  int conn_count_ap;
  int conn_count_dir;
  int conn_count_total;
} throughput_snapshot_t;

/** Previous snapshot, used to compute deltas. */
static throughput_snapshot_t prev_snapshot;
/** Whether prev_snapshot has been populated. */
static int snapshot_valid = 0;

/** Count current connections by type from the connection array. */
static void
count_connections_by_type(throughput_snapshot_t *snap)
{
  snap->conn_count_or = 0;
  snap->conn_count_exit = 0;
  snap->conn_count_ap = 0;
  snap->conn_count_dir = 0;
  snap->conn_count_total = 0;

  SMARTLIST_FOREACH_BEGIN(get_connection_array(), connection_t *, conn) {
    if (connection_is_listener(conn))
      continue;
    snap->conn_count_total++;
    switch (conn->type) {
    case CONN_TYPE_OR:
      snap->conn_count_or++;
      break;
    case CONN_TYPE_EXIT:
      snap->conn_count_exit++;
      break;
    case CONN_TYPE_AP:
      snap->conn_count_ap++;
      break;
    case CONN_TYPE_DIR:
      snap->conn_count_dir++;
      break;
    default:
      break;
    }
  } SMARTLIST_FOREACH_END(conn);
}

/** Populate a snapshot with current counter values. */
static void
take_snapshot(throughput_snapshot_t *snap)
{
  snap->timestamp = time(NULL);
  snap->bytes_read = get_bytes_read();
  snap->bytes_written = get_bytes_written();
  snap->cells_relayed = stats_n_relay_cells_relayed;
  snap->cells_delivered = stats_n_relay_cells_delivered;
  snap->cells_processed = stats_n_relay_cells_processed;
  snap->n_circuits = smartlist_len(circuit_get_global_list());
  snap->n_open_sockets = get_n_open_sockets();
  snap->buf_allocation = buf_get_total_allocation();
  snap->pending_tasks = cpuworker_get_pending_count();

  count_connections_by_type(snap);
}

/** Format bytes/sec as a human-readable rate string into buf.
 * Returns buf for convenience. */
static const char *
format_rate(char *buf, size_t buflen, uint64_t bytes, time_t elapsed)
{
  double rate = (double)bytes / (double)elapsed;
  if (rate >= 1024.0 * 1024.0) {
    tor_snprintf(buf, buflen, "%.1f MB/s", rate / (1024.0 * 1024.0));
  } else if (rate >= 1024.0) {
    tor_snprintf(buf, buflen, "%.1f KB/s", rate / 1024.0);
  } else {
    tor_snprintf(buf, buflen, "%.0f B/s", rate);
  }
  return buf;
}

/** Log throughput metrics.  On second and subsequent calls, reports
 * per-second rates computed from the delta since the previous call.
 * Called from process_signal(SIGUSR1) in main.c. */
void
dump_throughput_metrics(int severity)
{
  throughput_snapshot_t cur;
  take_snapshot(&cur);

  if (!snapshot_valid) {
    /* First call — no previous snapshot to diff against. */
    tor_log(severity, LD_HEARTBEAT,
            "Throughput: Total read: %.2f MB, Total written: %.2f MB "
            "(first snapshot, rates available next query)",
            (double)cur.bytes_read / (1 << 20),
            (double)cur.bytes_written / (1 << 20));
  } else {
    time_t elapsed = cur.timestamp - prev_snapshot.timestamp;
    if (elapsed < 1)
      elapsed = 1; /* avoid division by zero / clock jump */

    uint64_t d_read = cur.bytes_read - prev_snapshot.bytes_read;
    uint64_t d_written = cur.bytes_written - prev_snapshot.bytes_written;
    uint64_t d_relayed = cur.cells_relayed - prev_snapshot.cells_relayed;
    uint64_t d_delivered = cur.cells_delivered -
                           prev_snapshot.cells_delivered;

    char rbuf[32], wbuf[32];
    format_rate(rbuf, sizeof(rbuf), d_read, elapsed);
    format_rate(wbuf, sizeof(wbuf), d_written, elapsed);

    tor_log(severity, LD_HEARTBEAT,
            "Throughput: %s read, %s written over %d sec",
            rbuf, wbuf, (int)elapsed);

    tor_log(severity, LD_HEARTBEAT,
            "Throughput: %"PRIu64" cells/sec relayed, "
            "%"PRIu64" cells/sec delivered",
            d_relayed / (uint64_t)elapsed,
            d_delivered / (uint64_t)elapsed);
  }

  /* Instantaneous state (always logged) */
  tor_log(severity, LD_HEARTBEAT,
          "Throughput: Connections: %d OR, %d Exit, %d AP, %d Dir "
          "(%d total, %d sockets)",
          cur.conn_count_or, cur.conn_count_exit,
          cur.conn_count_ap, cur.conn_count_dir,
          cur.conn_count_total, cur.n_open_sockets);

  tor_log(severity, LD_HEARTBEAT,
          "Throughput: %d circuits open. Buffer memory: "
          "%"TOR_PRIuSZ" kB. CPU tasks pending: %u",
          cur.n_circuits, cur.buf_allocation / 1024,
          cur.pending_tasks);

  prev_snapshot = cur;
  snapshot_valid = 1;
}
