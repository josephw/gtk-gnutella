/*
 * Copyright (c) 2001-2003, Richard Eckart
 *
 * THIS FILE IS AUTOGENERATED! DO NOT EDIT!
 * This file is generated from gnet_props.ag using autogen.
 * Autogen is available at http://autogen.sourceforge.net/.
 *
 *----------------------------------------------------------------------
 * This file is part of gtk-gnutella.
 *
 *  gtk-gnutella is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  gtk-gnutella is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with gtk-gnutella; if not, write to the Free Software
 *  Foundation, Inc.:
 *      59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *----------------------------------------------------------------------
 */

#ifndef _gnet_property_priv_h_
#define _gnet_property_priv_h_

#include <glib.h>

extern gboolean reading_hostfile;
extern gboolean reading_ultrafile;
extern gboolean ancient_version;
extern gchar   *new_version_str;
extern guint32  up_connections;
extern guint32  normal_connections;
extern guint32  max_connections;
extern guint32  node_leaf_count;
extern guint32  node_normal_count;
extern guint32  node_ultra_count;
extern guint32  max_downloads;
extern guint32  max_host_downloads;
extern guint32  max_uploads;
extern guint32  max_uploads_ip;
extern guint32  local_ip;
extern guint32  current_ip_stamp;
extern guint32  average_ip_uptime;
extern guint32  start_stamp;
extern guint32  average_servent_uptime;
extern guint32  listen_port;
extern guint32  forced_local_ip;
extern guint32  connection_speed;
extern gboolean compute_connection_speed;
extern guint32  search_max_items;
extern guint32  ul_usage_min_percentage;
extern guint32  download_connecting_timeout;
extern guint32  download_push_sent_timeout;
extern guint32  download_connected_timeout;
extern guint32  download_retry_timeout_min;
extern guint32  download_retry_timeout_max;
extern guint32  download_max_retries;
extern guint32  download_retry_timeout_delay;
extern guint32  download_retry_busy_delay;
extern guint32  download_retry_refused_delay;
extern guint32  download_retry_stopped_delay;
extern guint32  download_overlap_range;
extern guint32  upload_connecting_timeout;
extern guint32  upload_connected_timeout;
extern guint32  search_reissue_timeout;
extern guint32  ban_ratio_fds;
extern guint32  ban_max_fds;
extern guint32  banned_count;
extern guint32  max_banned_fd;
extern guint32  incoming_connecting_timeout;
extern guint32  node_connecting_timeout;
extern guint32  node_connected_timeout;
extern guint32  node_sendqueue_size;
extern guint32  node_tx_flowc_timeout;
extern guint32  node_rx_flowc_ratio;
extern guint32  max_ttl;
extern guint32  my_ttl;
extern guint32  hard_ttl_limit;
extern guint32  dbg;
extern guint32  lib_debug;
extern guint32  gwc_debug;
extern guint32  url_debug;
extern guint32  dh_debug;
extern guint32  dq_debug;
extern guint32  track_props;
extern gboolean stop_host_get;
extern gboolean bws_in_enabled;
extern gboolean bws_out_enabled;
extern gboolean bws_gin_enabled;
extern gboolean bws_glin_enabled;
extern gboolean bws_gout_enabled;
extern gboolean bws_glout_enabled;
extern gboolean bw_ul_usage_enabled;
extern gboolean bw_allow_stealing;
extern gboolean clear_complete_downloads;
extern gboolean clear_failed_downloads;
extern gboolean clear_unavailable_downloads;
extern gboolean search_remove_downloaded;
extern gboolean force_local_ip;
extern gboolean use_netmasks;
extern gboolean allow_private_network_connection;
extern gboolean use_ip_tos;
extern gboolean download_delete_aborted;
extern gboolean proxy_auth;
extern gchar   *socks_user;
extern gchar   *socks_pass;
extern guint32  proxy_ip;
extern gchar   *proxy_hostname;
extern guint32  proxy_port;
extern guint32  proxy_protocol;
extern guint32  hosts_in_catcher;
extern guint32  hosts_in_ultra_catcher;
extern guint32  hosts_in_bad_catcher;
extern guint32  max_hosts_cached;
extern guint32  max_ultra_hosts_cached;
extern guint32  max_bad_hosts_cached;
extern guint32  max_high_ttl_msg;
extern guint32  max_high_ttl_radius;
extern guint32  bw_http_in;
extern guint32  bw_http_out;
extern guint32  bw_gnet_in;
extern guint32  bw_gnet_out;
extern guint32  bw_gnet_lin;
extern guint32  bw_gnet_lout;
extern guint32  search_queries_forward_size;
extern guint32  search_queries_kick_size;
extern guint32  search_answers_forward_size;
extern guint32  search_answers_kick_size;
extern guint32  other_messages_kick_size;
extern guint32  hops_random_factor;
extern gboolean send_pushes;
extern guint32  min_dup_msg;
extern guint32  min_dup_ratio;
extern gchar   *scan_extensions;
extern gboolean scan_ignore_symlink_dirs;
extern gboolean scan_ignore_symlink_regfiles;
extern gchar   *save_file_path;
extern gchar   *move_file_path;
extern gchar   *bad_file_path;
extern gchar   *shared_dirs_paths;
extern gchar   *local_netmasks_string;
extern guint32  total_downloads;
extern guint32  ul_running;
extern guint32  ul_registered;
extern guint32  total_uploads;
extern guint8   guid[16];
extern gboolean use_swarming;
extern gboolean use_aggressive_swarming;
extern guint32  dl_minchunksize;
extern guint32  dl_maxchunksize;
extern gboolean auto_download_identical;
extern gboolean auto_feed_download_mesh;
extern gboolean strict_sha1_matching;
extern gboolean use_fuzzy_matching;
extern guint32  fuzzy_threshold;
extern gboolean is_firewalled;
extern gboolean is_inet_connected;
extern gboolean gnet_compact_query;
extern gboolean download_optimistic_start;
extern gboolean library_rebuilding;
extern gboolean sha1_rebuilding;
extern gboolean sha1_verifying;
extern gboolean file_moving;
extern gboolean prefer_compressed_gnet;
extern gboolean online_mode;
extern gboolean download_require_urn;
extern gboolean download_require_server_name;
extern guint32  max_ultrapeers;
extern guint32  quick_connect_pool_size;
extern guint32  max_leaves;
extern guint32  search_handle_ignored_files;
extern guint32  configured_peermode;
extern guint32  current_peermode;
extern guint32  sys_nofile;
extern guint32  sys_physmem;
extern guint32  dl_queue_count;
extern guint32  dl_running_count;
extern guint32  dl_active_count;
extern guint32  dl_aqueued_count;
extern guint32  dl_pqueued_count;
extern guint32  fi_all_count;
extern guint32  fi_with_source_count;
extern guint32  dl_qalive_count;
extern guint64  dl_byte_count;
extern guint64  ul_byte_count;
extern gboolean pfsp_server;
extern guint32  pfsp_first_chunk;
extern gboolean fuzzy_filter_dmesh;
extern guint32  crawler_visit_count;
extern gboolean host_runs_ntp;
extern guint32  clock_skew;
extern gboolean node_monitor_unstable_ip;
extern gboolean node_monitor_unstable_servents;
extern gboolean dl_remove_file_on_mismatch;
extern guint32  dl_mismatch_backout;
extern gchar   *server_hostname;
extern gboolean give_server_hostname;
extern guint32  reserve_gtkg_nodes;
extern guint32  unique_nodes;
extern guint32  download_rx_size;
extern guint32  node_rx_size;
extern guint32  dl_http_latency;
extern guint32  node_last_ultra_check;
extern guint32  node_last_ultra_leaf_switch;
extern gboolean up_req_avg_servent_uptime;
extern gboolean up_req_avg_ip_uptime;
extern gboolean up_req_node_uptime;
extern gboolean up_req_not_firewalled;
extern gboolean up_req_enough_conn;
extern gboolean up_req_enough_fd;
extern gboolean up_req_enough_mem;
extern gboolean up_req_enough_bw;
extern guint32  search_queue_size;
extern guint32  search_queue_spacing;
extern gboolean enable_shell;
extern guint32  entry_removal_timeout;
extern gboolean node_watch_similar_queries;
extern guint32  node_queries_half_life;
extern guint32  node_requery_threshold;
extern guint32  library_rescan_timestamp;
extern guint32  library_rescan_time;
extern guint32  qrp_indexing_timestamp;
extern guint32  qrp_indexing_time;
extern guint32  qrp_timestamp;
extern guint32  qrp_computation_time;
extern guint32  qrp_patch_timestamp;
extern guint32  qrp_patch_computation_time;
extern guint32  qrp_generation;
extern guint32  qrp_slots;
extern guint32  qrp_slots_filled;
extern guint32  qrp_fill_ratio;
extern guint32  qrp_conflict_ratio;
extern guint32  qrp_hashed_keywords;
extern guint32  qrp_patch_raw_length;
extern guint32  qrp_patch_length;
extern guint32  qrp_patch_comp_ratio;
extern gchar   *ancient_version_force;
extern guint32  ancient_version_left_days;
extern gboolean file_descriptor_shortage;
extern gboolean file_descriptor_runout;
extern gboolean convert_spaces;


prop_set_t *gnet_prop_init(void);
void gnet_prop_shutdown(void);

#endif /* _gnet_property_priv_h_ */

