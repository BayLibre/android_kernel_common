// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Xiaomi Co., Ltd.
 *             http://www.xiaomi.com/
 */

#include <linux/seq_file.h>
#include "internal.h"
#include "iostat.h"

static const char *size_to_str(enum io_size_gran size)
{
	static const char * const size_str[] = {
		[SIZE_16K]   = "16",
		[SIZE_128K]  = "128",
		[SIZE_256K]  = "256",
		[SIZE_512K]  = "512",
		[SIZE_ABOVE] = "512+",
	};
	return size_str[size];
}

static const char *latency_range_to_str(enum latency_ranges range)
{
	static const char * const range_str[] = {
		[RANGE_1MS]    = "1",
		[RANGE_5MS]    = "5",
		[RANGE_10MS]   = "10",
		[RANGE_50MS]   = "50",
		[RANGE_ABOVE]  = "50+"
	};
	return range_str[range];
}

int erofs_iostat_config_parse(struct erofs_sb_info *sbi, const char *buf)
{
	struct erofs_iostat *iostat = sbi->iostat;
	char *str = strim((char *)buf);
	char *token;
	unsigned long thresholds[NR_IO_SIZE];
	unsigned long period;
	int i = 0;
	int ret = 0;

	if (!iostat)
		return -EINVAL;

	if (!str)
		return -ENOMEM;

	token = strsep(&str, ",");
	if (!token)
		return -EINVAL;

	ret = kstrtoul(token, 10, &period);
	if (ret || period > UINT_MAX)
		return -EINVAL;

	for (i = 0; i < NR_IO_SIZE; i++) {
		token = strsep(&str, ",");
		if (!token)
			return -EINVAL;

		ret = kstrtoul(token, 10, &thresholds[i]);
		if (ret)
			return -EINVAL;
	}

	if (str)
		return -EINVAL;

	iostat->window_period_ns = period * NSEC_PER_SEC;
	for (i = 0; i < NR_IO_SIZE; i++)
		iostat->latency_threshold_ns[i] = thresholds[i] * NSEC_PER_MSEC;

	return 0;
}

int erofs_iostat_daily_stats_reset(struct erofs_sb_info *sbi, const char *buf)
{
	struct erofs_iostat *iostat = sbi->iostat;
	struct erofs_iostat_stats *stats;
	const char *str = strim((char *)buf);
	int cpu;

	if (!iostat || !iostat->iostat_enable)
		return -ENODEV;

	if (strncmp(str, "reset", 5) != 0)
		return -EINVAL;

	for_each_possible_cpu(cpu) {
		stats = per_cpu_ptr(iostat->stats, cpu);
		memset(stats->daily_stats, 0, sizeof(stats->daily_stats));
		memset(stats->daily_ab_stats, 0, sizeof(stats->daily_ab_stats));
		memset(&stats->delay_stats, 0, sizeof(stats->delay_stats));
	}

	return 0;
}

ssize_t erofs_iostat_daily_latency_show(struct erofs_sb_info *sbi, char *buf)
{
	struct erofs_iostat *iostat = sbi->iostat;
	struct erofs_iostat_stats *stats;
	enum io_size_gran size;
	ssize_t len = 0;
	unsigned long sum_lat, peak_lat;
	unsigned int bio_cnt;
	int cpu;

	if (!iostat || !iostat->iostat_enable)
		return sysfs_emit(buf, "I/O stats not available\n");

	len += sysfs_emit_at(buf, len, "Normal:\n%-12s %-16s %-16s %-16s\n",
				"Size(k)", "Sum(ms)", "Count", "Peak(ms)");

	for (size = 0; size < NR_IO_SIZE; size++) {
		sum_lat = 0;
		peak_lat = 0;
		bio_cnt = 0;

		for_each_possible_cpu(cpu) {
			stats = per_cpu_ptr(iostat->stats, cpu);
			sum_lat += stats->daily_stats[size].sum_lat;
			bio_cnt += stats->daily_stats[size].bio_cnt;
			peak_lat = max(stats->daily_stats[size].peak_lat, peak_lat);
		}

		len += sysfs_emit_at(buf, len,
			"%-12s %-16llu %-16u %-16llu\n",
			size_to_str(size),
			ktime_to_ms(sum_lat),
			bio_cnt,
			ktime_to_ms(peak_lat));
	}

	len += sysfs_emit_at(buf, len, "\nAbnormal:\n%-12s %-16s %-16s %-16s %-16s\n",
				"Size(k)", "Sum(ms)", "Count", "Peak(ms)", "Threshold(ms)");

	for (size = 0; size < NR_IO_SIZE; size++) {
		sum_lat = 0;
		peak_lat = 0;
		bio_cnt = 0;

		for_each_possible_cpu(cpu) {
			stats = per_cpu_ptr(iostat->stats, cpu);
			sum_lat += stats->daily_ab_stats[size].sum_lat;
			bio_cnt += stats->daily_ab_stats[size].bio_cnt;
			peak_lat = max(stats->daily_ab_stats[size].peak_lat, peak_lat);
		}

		len += sysfs_emit_at(buf, len,
			"%-12s %-16llu %-16u %-16llu %-16lu\n",
			size_to_str(size),
			ktime_to_ms(sum_lat),
			bio_cnt,
			ktime_to_ms(peak_lat),
			iostat->latency_threshold_ns[size] / NSEC_PER_MSEC);
	}

	len += sysfs_emit_at(buf, len, "\nDelay:\n%-10s %-16s %-11s %-16s\n",
			"Range(ms)", "Sum(ms)", "Count", "Peak(ms)");

	for (enum latency_ranges range = 0; range < NR_RANGES; range++) {
		sum_lat = 0;
		peak_lat = 0;
		bio_cnt = 0;
		for_each_possible_cpu(cpu) {
			stats = per_cpu_ptr(iostat->stats, cpu);
			sum_lat += stats->delay_stats[range].sum_lat;
			bio_cnt += stats->delay_stats[range].bio_cnt;
			peak_lat = max(stats->delay_stats[range].peak_lat, peak_lat);
		}

		len += sysfs_emit_at(buf, len,
			"%-10s %-16llu %-11u %-16llu\n",
			latency_range_to_str(range),
			ktime_to_ms(sum_lat),
			bio_cnt,
			ktime_to_ms(peak_lat));
	}

	return len;
}

ssize_t erofs_iostat_window_latency_show(struct erofs_sb_info *sbi, char *buf)
{
	struct erofs_iostat *iostat = sbi->iostat;
	struct erofs_iostat_stats *stats;
	struct latency_stats *lat, *ab_lat;
	enum io_size_gran size;
	int window, idx;
	ssize_t len = 0;
	int cpu;
	unsigned long sum_lat, peak_lat;
	unsigned int bio_cnt;
	unsigned long period_ns;
	unsigned long now = ktime_get_ns();
	unsigned long elapsed_ns;

	if (!iostat || !iostat->iostat_enable)
		return sysfs_emit(buf, "I/O stats not available\n");

	period_ns = iostat->window_period_ns;

	len += sysfs_emit_at(buf, len, "Normal:\n%-8s %-12s %-16s %-16s %-16s\n",
			"Window", "Size(k)", "Sum(ms)", "Count", "Peak(ms)");

	for (window = 0; window < TIME_WINDOWS; window++) {
		for (size = 0; size < NR_IO_SIZE; size++) {
			sum_lat = 0;
			peak_lat = 0;
			bio_cnt = 0;

			for_each_possible_cpu(cpu) {
				stats = per_cpu_ptr(iostat->stats, cpu);
				elapsed_ns = now - stats->current_window_start;
				if (elapsed_ns > (window + 2) * period_ns)
					continue;
				idx = (stats->current_window_idx - window + TIME_WINDOWS)
						% TIME_WINDOWS;
				lat = &stats->window_stats[idx][size];
				sum_lat += lat->sum_lat;
				bio_cnt += lat->bio_cnt;
				peak_lat = max(lat->peak_lat, peak_lat);
			}
			if (bio_cnt > 0) {
				len += sysfs_emit_at(buf, len,
				"%-8d %-12s %-16llu %-16u %-16llu\n",
				idx,
				size_to_str(size),
				ktime_to_ms(sum_lat),
				bio_cnt,
				ktime_to_ms(peak_lat));
			}
		}
	}

	len += sysfs_emit_at(buf, len, "\nAbnormal:\n%-8s %-12s %-16s %-16s %-16s %-16s\n",
			"Window", "Size", "Sum(ms)", "Count", "Peak(ms)", "Threshold(ms)");

	for (window = 0; window < TIME_WINDOWS; window++) {
		for (size = 0; size < NR_IO_SIZE; size++) {
			sum_lat = 0;
			peak_lat = 0;
			bio_cnt = 0;

			for_each_possible_cpu(cpu) {
				stats = per_cpu_ptr(iostat->stats, cpu);
				elapsed_ns = now - stats->current_window_start;
				if (elapsed_ns > (window + 2) * period_ns)
					continue;
				idx = (stats->current_window_idx - window + TIME_WINDOWS)
						% TIME_WINDOWS;
				ab_lat = &stats->window_ab_stats[idx][size];
				sum_lat += ab_lat->sum_lat;
				bio_cnt += ab_lat->bio_cnt;
				peak_lat = max(ab_lat->peak_lat, peak_lat);
			}

			if (bio_cnt > 0) {
				len += sysfs_emit_at(buf, len,
				"%-8d %-12s %-16llu %-16u %-16llu %-16lu\n",
				idx,
				size_to_str(size),
				ktime_to_ms(sum_lat),
				bio_cnt,
				ktime_to_ms(peak_lat),
				iostat->latency_threshold_ns[size] / NSEC_PER_MSEC);
			}
		}
	}

	return len;
}

void erofs_iostat_latency_stats_reset(struct erofs_sb_info *sbi)
{
	struct erofs_iostat *iostat = sbi->iostat;
	struct erofs_iostat_stats *stats;
	int cpu;

	for_each_possible_cpu(cpu) {
		stats = per_cpu_ptr(iostat->stats, cpu);
		memset(&stats->window_stats, 0, sizeof(stats->window_stats));
		memset(&stats->window_ab_stats, 0, sizeof(stats->window_ab_stats));
		memset(&stats->daily_stats, 0, sizeof(stats->daily_stats));
		memset(&stats->daily_ab_stats, 0, sizeof(stats->daily_ab_stats));
		memset(&stats->delay_stats, 0, sizeof(stats->delay_stats));
	}
}

static inline enum io_size_gran get_io_size_granularity(unsigned int bio_size_bytes)
{
	unsigned int blks = bio_size_bytes >> 10;

	return (blks > 16) + (blks > 128) + (blks > 256) + (blks > 512);
}

static inline enum latency_ranges get_latency_range(unsigned long latency)
{
	if (latency < DELAY_RANGE_1MS)
		return RANGE_1MS;
	else if (latency < DELAY_RANGE_5MS)
		return RANGE_5MS;
	else if (latency < DELAY_RANGE_10MS)
		return RANGE_10MS;
	else if (latency < DELAY_RANGE_50MS)
		return RANGE_50MS;
	else
		return RANGE_ABOVE;
}

static unsigned int update_time_windows(struct erofs_sb_info *sbi, unsigned long end_ts)
{
	struct erofs_iostat *iostat = sbi->iostat;
	struct erofs_iostat_stats *stats = this_cpu_ptr(iostat->stats);
	unsigned long elapsed_ns = end_ts - stats->current_window_start;
	unsigned long period_ns = iostat->window_period_ns;

	if (elapsed_ns > period_ns) {
		stats->current_window_start = end_ts;
		stats->current_window_idx = (stats->current_window_idx + 1) % TIME_WINDOWS;

		memset(&stats->window_stats[stats->current_window_idx], 0,
			  sizeof(stats->window_stats[stats->current_window_idx]));
		memset(&stats->window_ab_stats[stats->current_window_idx], 0,
			  sizeof(stats->window_ab_stats[stats->current_window_idx]));
	}

	return stats->current_window_idx;
}

static inline void __update_iostat_latency(struct erofs_sb_info *sbi,
					 u64 submit_ts, unsigned int bio_size_bytes)
{
	struct erofs_iostat *iostat = sbi->iostat;
	struct erofs_iostat_stats *stats;
	unsigned long latency, end_ts;
	enum io_size_gran gran;
	unsigned int window_idx;
	enum latency_ranges range;

	if (!iostat->iostat_enable)
		return;

	end_ts = ktime_get_ns();
	latency = end_ts - submit_ts;

	gran = get_io_size_granularity(bio_size_bytes);
	range = get_latency_range(latency);

	preempt_disable();
	window_idx = update_time_windows(sbi, end_ts);

	stats = this_cpu_ptr(iostat->stats);
	stats->window_stats[window_idx][gran].sum_lat += latency;
	stats->window_stats[window_idx][gran].bio_cnt++;
	stats->window_stats[window_idx][gran].peak_lat =
		max(stats->window_stats[window_idx][gran].peak_lat, latency);

	stats->daily_stats[gran].sum_lat += latency;
	stats->daily_stats[gran].bio_cnt++;
	stats->daily_stats[gran].peak_lat =
		max(stats->daily_stats[gran].peak_lat, latency);

	if (latency > iostat->latency_threshold_ns[gran]) {
		stats->window_ab_stats[window_idx][gran].sum_lat += latency;
		stats->window_ab_stats[window_idx][gran].bio_cnt++;
		stats->window_ab_stats[window_idx][gran].peak_lat =
			stats->window_stats[window_idx][gran].peak_lat;
		stats->daily_ab_stats[gran].sum_lat += latency;
		stats->daily_ab_stats[gran].bio_cnt++;
		stats->daily_ab_stats[gran].peak_lat =
			stats->daily_stats[gran].peak_lat;
	}

	stats->delay_stats[range].sum_lat += latency;
	stats->delay_stats[range].bio_cnt++;
	stats->delay_stats[range].peak_lat =
		max(stats->delay_stats[range].peak_lat, latency);
	preempt_enable();
}

void erofs_iostat_update(struct erofs_sb_info *sbi, struct bio *bio)
{
	struct erofs_iostat *iostat = sbi->iostat;
	unsigned int bio_size_bytes = bio->bi_iter.bi_size;
	u64 submit_ts;

	if (!iostat->iostat_enable)
		return;

	if (bio->android_oem_data1 == 0)
		return;

	submit_ts = bio->android_oem_data1;
	__update_iostat_latency(sbi, submit_ts, bio_size_bytes);
	bio->android_oem_data1 = 0;
}

void erofs_iostat_record_start(struct erofs_sb_info *sbi, struct bio *bio)
{
	if (!sbi->iostat->iostat_enable)
		return;

	bio->android_oem_data1 = ktime_get_ns();
}

int erofs_init_iostat(struct erofs_sb_info *sbi)
{
	int cpu;

	sbi->iostat = kzalloc(sizeof(struct erofs_iostat),
					GFP_KERNEL);

	if (!sbi->iostat)
		return -ENOMEM;

	sbi->iostat->iostat_enable = false;
	sbi->iostat->window_period_ns = DEFAULT_WINDOW_PERIOD * NSEC_PER_SEC;
	sbi->iostat->latency_threshold_ns[SIZE_16K]   = THRES_MS_SIZE_16K   * NSEC_PER_MSEC;
	sbi->iostat->latency_threshold_ns[SIZE_128K]  = THRES_MS_SIZE_128K  * NSEC_PER_MSEC;
	sbi->iostat->latency_threshold_ns[SIZE_256K]  = THRES_MS_SIZE_256K  * NSEC_PER_MSEC;
	sbi->iostat->latency_threshold_ns[SIZE_512K]  = THRES_MS_SIZE_512K  * NSEC_PER_MSEC;
	sbi->iostat->latency_threshold_ns[SIZE_ABOVE] = THRES_MS_SIZE_ABOVE * NSEC_PER_MSEC;

	sbi->iostat->stats = alloc_percpu(struct erofs_iostat_stats);
	if (!sbi->iostat->stats) {
		kfree(sbi->iostat);
		return -ENOMEM;
	}
	for_each_possible_cpu(cpu) {
		struct erofs_iostat_stats *stats = per_cpu_ptr(sbi->iostat->stats, cpu);

		stats->current_window_idx = 0;
		stats->current_window_start = ktime_get_ns();

		memset(stats->window_stats, 0, sizeof(stats->window_stats));
		memset(stats->window_ab_stats, 0, sizeof(stats->window_ab_stats));
		memset(stats->daily_stats, 0, sizeof(stats->daily_stats));
		memset(stats->daily_ab_stats, 0, sizeof(stats->daily_stats));
		memset(stats->delay_stats, 0, sizeof(stats->delay_stats));
	}

	return 0;
}

void erofs_destroy_iostat(struct erofs_sb_info *sbi)
{
	if (sbi->iostat) {
		if (sbi->iostat->stats)
			free_percpu(sbi->iostat->stats);
		kfree(sbi->iostat);
		sbi->iostat = NULL;
	}
}
