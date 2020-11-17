PROTO_FLAG(uint64_t,FLAG_max_send_buf_size,1024u)
PROTO_FLAG(uint32_t,FLAG_packet_payload,1400u)
// Congestion window gain for QUIC BBR during PROBE_BW phase.
PROTO_FLAG(double, FLAG_quic_bbr_cwnd_gain, 2.0f)

// Simplify QUIC\'s adaptive time loss detection to measure the necessary
// reordering window for every spurious retransmit.
PROTO_FLAG(bool, FLAGS_quic_reloadable_flag_quic_fix_adaptive_time_loss, false)

// If true, adjust congestion window when doing bandwidth resumption in BBR.
PROTO_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_fix_bbr_cwnd_in_bandwidth_resumption,
          true)

// When true, defaults to BBR congestion control instead of Cubic.
PROTO_FLAG(bool, FLAG_quic_reloadable_flag_quic_default_to_bbr, false)
// If true, in BbrSender, always get a bandwidth sample when a packet is acked,
// even if packet.bytes_acked is zero.
PROTO_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_always_get_bw_sample_when_acked,
          true)

// When you\'re app-limited entering recovery, stay app-limited until you exit
// recovery in QUIC BBR.
PROTO_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_app_limited_recovery, false)


// When true, ensure BBR allows at least one MSS to be sent in response to an
// ACK in packet conservation.
PROTO_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_one_mss_conservation, false)

// Add 3 connection options to decrease the pacing and CWND gain in QUIC BBR
// STARTUP.
PROTO_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_slower_startup3, true)

// When true, the LOSS connection option allows for 1/8 RTT of reording instead
// of the current 1/8th threshold which has been found to be too large for fast
// loss recovery.
PROTO_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_eighth_rtt_loss_detection,
          false)

// Enables the BBQ5 connection option, which forces saved aggregation values to
// expire when the bandwidth increases more than 25% in QUIC BBR STARTUP.
PROTO_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_slower_startup4, false)

// When true and the BBR9 connection option is present, BBR only considers
// bandwidth samples app-limited if they're not filling the pipe.
PROTO_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_flexible_app_limited, false)

// When in STARTUP and recovery, do not add bytes_acked to QUIC BBR's CWND in
// CalculateCongestionWindow()
PROTO_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_bbr_no_bytes_acked_in_startup_recovery,
    false)

// Number of packets that the pacing sender allows in bursts during pacing.
PROTO_FLAG(int32_t, FLAG_quic_lumpy_pacing_size, 1)

// Congestion window fraction that the pacing sender allows in bursts during
// pacing.
PROTO_FLAG(double, FLAG_quic_lumpy_pacing_cwnd_fraction, 0.25f)
// If true, disable lumpy pacing for low bandwidth flows.
PROTO_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_no_lumpy_pacing_at_low_bw,
          false)
// If true, stop resetting ideal_next_packet_send_time_ in pacing sender.
PROTO_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_donot_reset_ideal_next_packet_send_time,
    false)
// Max time that QUIC can pace packets into the future in ms.
PROTO_FLAG(int32_t, FLAGS_quic_max_pace_time_into_future_ms, 10)

// Smoothed RTT fraction that a connection can pace packets into the future.
PROTO_FLAG(double, FLAGS_quic_pace_time_into_future_srtt_fraction, 0.125f)

// Congestion window gain for QUIC BBR during PROBE_BW phase.
PROTO_FLAG(double, FLAGS_quic_bbr_cwnd_gain, 2.0f)
// The default minimum number of loss marking events to exit STARTUP.
PROTO_FLAG(int32_t, FLAGS_quic_bbr2_default_startup_full_loss_count, 8)

// The default loss threshold for QUIC BBRv2, should be a value
// between 0 and 1.
PROTO_FLAG(double, FLAGS_quic_bbr2_default_loss_threshold, 0.02)

// If true, QUIC BBRv2\'s PROBE_BW mode will not reduce cwnd below
// BDP+ack_height.
PROTO_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr2_avoid_too_low_probe_bw_cwnd,
          false)
PROTO_FLAG(float,
          FLAGS_quic_reloadable_flag_reno_beta,0.7)
