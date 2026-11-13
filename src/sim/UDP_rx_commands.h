#pragma once

#pragma pack( push, UDP_RX_COMMANDS_H )
#pragma pack( 8 )

// Data must fall on an 8 byte boundary and must be sent little endian.

namespace UDP_rx_command
{
    // Update the header_version number here when this file changes as it may be supplied to customers.
    // The SimREMOTE manual(DGP00792AAA) needs to be kept up-to-date with the structures and data
    // member offsets in this file.
    //
    const double header_version = 4.02;         // V1.01 November 2011 added IRNSS to SigType enum
                                                // V1.02 June 2015 added Azimuth and Elevation overrides
                                                //       to Mod_data structure. Overall size should not
                                                //       have changed; backwards compatible to V1.01 so
                                                //       long as entire V1.01 structure is zeroed before
                                                //       setting fields.
                                                // V1.03 November 2017 added Mp_switch_data structure.
                                                //       Overall size should not have changed
                                                // V1.04 Flex/Multi-constellation/New GTx. Removed
                                                //       obsolete GSM and BASE_STATION.
                                                // V2.00 Feb 2019. Remove unions/version number field.
                                                //       NO LONGER BINARY COMPATIBLE WITH V1.xx.
                                                // V2.01 September 2019. Added mod2 type and changed multipath type to
                                                //       allow -1 parameter for Mod/Mod2 commands. Overall size should not have changed
                                                // V3.00 April 2020. Changes to improve future binary compatibility while retaining binary
                                                //       compatibility with V2.01. There is no longer a single Command struct containing a
                                                //       union of every command type, there is now a separate struct for every command type.
                                                //       This allows new commands to be added without effecting other commands.
                                                //       Also added MOTQ and MOTBQ.
                                                // V3.01 September 2020. Added MOD_V2 command variant in support of antenna polarisation selection
                                                // V4.00 February 2021. time_of_validity_ms_ is now 64 bit double precision milliseconds
                                                //                      mod_v2 command type and structure deprecated and renamed to mod_lhcp
                                                //                      Legacy command types and structure removed but still supported
                                                //                      V3.0 command types removed but still supported
                                                // V4.01 May 2021. Added vehicle_type_ field to header to support spoof vehicles
                                                //                 Backwards compatible with V4.00 format the Vehicle type will be == 0
                                                //                 (real vehicle)
                                                // V4.02 15 Jun 2021. Added FADER and FADER_UPDATE command
                                                //                  These do not exist currently. So, no backward compatibility needed

    const int MAX_NUM_MPATHS = 32;
    const int MAX_SATS = 2;
    const int MAX_SUB_CHANS_EX = 20;
    const int MULTIPATH_INDEX_FOR_ALL_ECHOES = -999;

    const size_t max_udp_msg_size_bytes = 1500;

    struct Mot_ecef_dynamics
    {
        double position_ecef_xyz_[ 3 ];
        double velocity_mps_xyz_[ 3 ];
        double acceleration_mps2_xyz_[ 3 ];
        double jerk_mps3_xyz_[ 3 ];
    };

    struct Mot_llh_dynamics
    {
        double latitude_;
        double longitude_;
        double height_;
        double velocity_ned_mps_[ 3 ];
        double acceleration_ned_mps2_[ 3 ];
        double jerk_ned_mps3_[ 3 ];
    };

    struct Mot_angular_dynamics
    {
        // About body axis
        double velocity_radps_xyz_[ 3 ];
        double acceleration_radps_xyz_[ 3 ];
        double jerk_radsps_xyz_[ 3 ];
    };

    struct Mot_data     // SimREMOTE MOT command
    {
        Mot_ecef_dynamics ecef_dynamics_;
        double attitude_heb_[ 3 ];
        Mot_angular_dynamics angular_dynamics_;
    };

    struct Motb_data    // SimREMOTE MOTB command
    {
        Mot_llh_dynamics llh_dynamics_;
        double attitude_heb_[ 3 ];
        Mot_angular_dynamics angular_dynamics_;
    };

    struct Motq_data    // SimREMOTE MOTQ command
    {
        Mot_ecef_dynamics ecef_dynamics_;
        double attitude_quaternion_[ 4 ];
        Mot_angular_dynamics angular_dynamics_;
    };

    struct Motbq_data    // SimREMOTE MOTBQ command
    {
        Mot_llh_dynamics llh_dynamics_;
        double attitude_quaternion_[ 4 ];
        Mot_angular_dynamics angular_dynamics_;
    };

    enum SigType
    {
        gps = 0,
        waas,
        egnos,
        msas,
        gagan,
        sdcm,
        glonass,
        galileo,
        qz,
        irnss,
        beidou,
        caps,
        ground_tx,
        gtx_glonass,
        gtx_beidou,
        gtx_galileo,
        jammer,
        laas,
        gtx_generic,
        num_sig_types
    };

    static const unsigned int max_constellation_ids = 13;

    struct Constellation
    {
        // The constellation_id_ is only for use with simulators
        // licensed for multi-constellation operation.
        // Always set to 0(zero) if this feature is not licensed.
        // For further information contact Spirent Communications.
        //
        SigType         signal_type_;
        unsigned int    constellation_id_;
    };

    struct Mod_data
    {
        enum Mode
        {
            svid = 0,
            channel
        };

        enum Applicability
        {
            single = 0,
            all
        };

        enum Frequency
        {
            L1 = 0,
            L2,
            L5,
            L6,
            LX,
            LY,
            num_frequencies
        };

        // Starts from 1
        unsigned int    antenna_id_;

        Constellation   constellation_;
        unsigned int    svid_or_channel_no_;
        int             multipath_index_;
        Mode            mode_;
        Applicability   all_channels_;

        Frequency       frequency_;
        Applicability   all_frequencies_;

        char            spare1[4]; // pad to 8-byte boundary!!

        double          signal_level_;
        double          carrier_offset_;
        double          code_offset_;

        int             use_overrides_;

        char            spare2[4]; // pad to 8-byte boundary!!

        double          azimuth_override_;
        double          elev_override_;
    };

    struct Mp_switch_data
    {
        enum Mode
        {
            remove_mpaths_keep_los = 0,
            add_mpaths_keep_los,
            remove_mpaths_remove_los,
            add_mpaths_remove_los
        };

        struct Mpaths
        {
            Constellation   constellation_;
            unsigned int    svid_;
            Mode            add_remote_option_;
            unsigned int    num_of_mpaths_;
            unsigned short  chans_[MAX_NUM_MPATHS];
        };

        unsigned int  antenna_id_; // Starts from 1

        unsigned int  number_of_sats_;
        Mpaths mpaths_[MAX_SATS];
    };
    
    struct Fader_data
    {
        enum Frequency
        {
            ALL = -1,
            L1 = 0,
            L2,
            L5,
            L6,
            LX,
            LY,
            num_frequencies
        };

        struct Sub_chan
        {
            double level_ofs_;
            double delay_ofs_;
            double phase_ofs_;
        };

        unsigned int    antenna_id_;         // Antenna ID starts from 1

        Constellation   constellation_;
        unsigned int    svid_;
        int             multipath_index_;   // ALL - all multipath including LOS ( we use -999) and 0 for incident signal and 1-16
        Frequency       frequency_;
        double          los_level_offset_;
        unsigned int    num_sub_channels_;
        char            spare[4]; // pad to 8-byte boundary!!
        Sub_chan        sub_chan_info_[ MAX_SUB_CHANS_EX ];
    };
	
    struct Fader_update_data
    {
        enum Frequency
        {
            ALL = -1,
            L1 = 0,
            L2,
            L5,
            L6,
            LX,
            LY,
            num_frequencies
        };

        struct Sub_chan
        {
            double level_ofs_;
            double delay_ofs_;
            double phase_ofs_;
        };

        unsigned int    antenna_id_;         // Antenna ID starts from 1

        Constellation   constellation_;
        unsigned int    svid_;
        int             multipath_index_;   // ALL - all multipath including LOS ( we use -999) and 0 - Incident signal and 1-16
        Frequency       frequency_;
        double          los_level_offset_;
        unsigned int    echo_bit_mask_;
        char            spare[4]; // pad to 8-byte boundary!!
        Sub_chan        sub_chan_info_[ MAX_SUB_CHANS_EX ];
    };

    struct Header
    {
        enum Type
        {
            // Upon each major version change these enum values are shifted up accordingly.
            // The types in the previous major version are supported as legacy
            //
            start = 100,
            mot = start,
            motb,
            mod,
            mp_switch,
            mod2,
            motq,
            motbq,
            mod_v2, // deprecated will be removed, replaced by mod_lhcp
            mod_lhcp = mod_v2,
            fader,
            fader_update
        };

        enum Vehicle_type
        {
            real_veh,   // the normal case
            spoof_veh   // for a spoof vehicle
        };

        Type type_;

        enum Time_action
        {
            action_immediately = 0,
            action_at_timestamp
        };

        Time_action  time_action_;
        double       time_of_validity_ms_;

        uint16_t vehicle_id_;    // vehicle or spoofer index, 1..n
        uint16_t vehicle_type_;  // set to real_veh or spoof_veh from the enum Type above

        char spare[4]; // pad to 8-byte boundary!!
    };

    struct Command_mot
    {
        Header header_;
        Mot_data data_;
    };

    struct Command_motb
    {
        Header header_;
        Motb_data data_;
    };

    struct Command_motq
    {
        Header header_;
        Motq_data data_;
    };

    struct Command_motbq
    {
        Header header_;
        Motbq_data data_;
    };

    struct Command_mod
    {
        Header header_;
        Mod_data data_;
    };

    struct Command_mp_switch
    {
        Header header_;
        Mp_switch_data data_;
    };

    struct Command_fader
    {
        Header header_;
        Fader_data data_;
    };

    struct Command_fader_update
    {
        Header header_;
        Fader_update_data data_;
    };


    struct Command_mod_lhcp
    {
        Header header_;
        Mod_data data_;
        double lhcp_signal_level_;
    };

    typedef Command_mod_lhcp Command_mod_v2; // Command_mod_v2 deprecated will be removed, replaced by Command_mod_lhcp
}
#pragma pack( pop, UDP_RX_COMMANDS_H )
