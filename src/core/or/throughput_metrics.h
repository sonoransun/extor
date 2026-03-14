/* Copyright (c) 2024, The Tor Project, Inc. */
/* See LICENSE for licensing information */

/**
 * @file throughput_metrics.h
 * @brief Header for throughput_metrics.c — rate-based performance
 *        metrics reported via SIGUSR1.
 **/

#ifndef TOR_THROUGHPUT_METRICS_H
#define TOR_THROUGHPUT_METRICS_H

void dump_throughput_metrics(int severity);

#endif /* !defined(TOR_THROUGHPUT_METRICS_H) */
