#ifndef ETHERNET_SHARE_H
#define ETHERNET_SHARE_H

/*************************************************************************
 Defines
*************************************************************************/
#if _MSC_VER > 1000
#pragma once
#endif

/*************************************************************************
 The packing is default 8 but make it explicit - this means that all
 members of the structure are packed on an 8 byte boundary. Note that
 where msvc would put in hidden fields we have filled the holes with
 "spares". This should help with making this header file portable.
*************************************************************************/
#if _MSC_VER > 1000
#pragma pack( push, ETHERNET_SHARE_H1 )
#else
#pragma pack( push, ETHERNET_SHARE_H1, 8 )
#endif
#pragma pack( 8 )

/*************************************************************************
 This is the definition for the binary data transfer of data from the
 Ethernet option of simgen. We namespace this so not to cause naming
 conflict. The data is transmitted LSB and the doubles are formated as
 IEEE754. The data is aligned on a 8 byte boundary.

 -----------------------------------------------------------------------
 | Version | Comments                                                  |
 |---------------------------------------------------------------------|
 | 8       | This is a major change as the packet sizes have increased |
 |         | you will need to recompile your code with the latest      |
 |         | header file and use the latest datastreaming.dll to work  |
 |         | with SimGEN V2.70                                         |
 |---------------------------------------------------------------------|
 | 9       | This is a minor enum value change to support multiple     |
 |         | GPS constellations. Version 8 and 9 are compatible. We    |
 |         | have also added a type field to the Keplerian, LNav and   |
 |         | MNav data packets.                                        |
 -----------------------------------------------------------------------
 | 10      | This is a minor change to support the car motion sensor   |
 |         | data (generated on the Adlink 6208V and 7300A PCI cards   |
 |         | with SimAUTO). The packet sizes have not increased.       |
 |         |                                                           |
 |         | Have also added spare fields to fill out the hidden ones  |
 |         | placed by MSVC when packing to 8 byte boundary.           |
 |---------------------------------------------------------------------|
 | 11      | Minor changes                                             |
 |         |                                                           |
 |         | Have added an extra field  to define reference frame      |
 |         | types for the ins error model. This has been done so that |
 |         | the data streaming logger utility can  adjust labels      |
 |         | according to local( N,E,D) or body(X,Y,Z)                 |
 |         |                                                           |
 |         | Have added the field model_num to the Truth_Aiding_EGI    |
 |         | structure to allow for multiple ins models per vehicle.   |
 |         |                                                           |
 |         | The total packet size has not increased.                  |
 |---------------------------------------------------------------------|
 | 12      | Minor changes                                             |
 |         |                                                           |
 |         | Extra messages to support ULTS GANSS messages.            |
 |         | Supports mainly GLONASS assistance messages.              |
 |         |                                                           |
 |         | Added serial poll byte to status change message           |
 |         | Added new enums to Frequency types                        |
 |---------------------------------------------------------------------|
 | 13      | Minor changes                                             |
 |         |                                                           |
 |         | Sync message has been assigned the fixed number 36.       |
 |         |                                                           |
 |         | IMU_data vehicle number now starts at 1 to be consistent  |
 |         | with all other packets.                                   |
 |         |                                                           |
 |         | IMU_data antenna number has been changed to model_number  |
 |         | as antenna number had no meaning within this packet. The  |
 |         | model_number starts from 1 and relates to the Aiding -->  |
 |         | INS model enabled for "inertial IMU" output(1..4).       |
 |---------------------------------------------------------------------|
 | 14      | Minor change - to ULTS_GLONASS_Alm_Model only             |
 |         |                                                           |
 |         | Added "Signals available" field from GANSS Auxiliary      |
 |         | information message(3GPPS Table A.55.19) to              |
 |         | ULTS_GLONASS_Alm_Model struct                             |
 |---------------------------------------------------------------------|
 | 15      | Minor change - added QZ L2 and L5 frequencies             |
 |         | Minor change - extra parameters added to INS_Error_Model. |
 |---------------------------------------------------------------------|
 | 16      | Minor change - extra parameters added to Antenna          |
 |         |                for enhanced vehicle spinning model.       |
 |---------------------------------------------------------------------|
 | 17      | Minor change - 23/11/2011 Added IRNSS to signal type      |
 |         |                enums for PosApp V4.00                     |
 |---------------------------------------------------------------------|
 | 18      | Minor change - 11/04/2012 Added signal type->analog op    |
 |         |                mapping in car sensor msg for PosApp V4.02 |
 |         |                                                           |
 |         | Minor change - 20/04/2012 Added BEIDOU to signal type     |
 |         |                enums for PosApp V4.02                     |
 |         |                                                           |
 |         | Minor change - 28/05/2012 Added CAPS to signal type       |
 |         |                enums for PosApp V4.02                     |
 |         |                                                           |
 |         | Minor change - 20/06/2012 Added SimSENSOR packet and      |
 |         |                change INTERNAL IMU back to what it was    |
 |         |                                                           |
 |         | Minor change - 10/09/2012 Added vehicle ID to CMS packet  |
 |         |                for V4.02 SR01                             |
 |---------------------------------------------------------------------|
 | 19      | Minor change - 04/07/2013 Added support for GLONASS GTx   |
 |---------------------------------------------------------------------|
 | 20      | Minor change - 13/01/2014 Support for ULTS Beidou assist  |
 |---------------------------------------------------------------------|
 | 21.00   | Major change - 07/03/2014 Added support for the C50 &     |
 |         |                9000 working at 1 KHz update rate. The     |
 |         |                packet size has increased to allow for     |
 |         |                better Ethernet bandwidth utilisation and  |
 |         |                the version now consists of two parts      |
 |         |                major and minor. A major number change     |
 |         |                means a re-compile is necessary. A minor   |
 |         |                change means that something has been added |
 |         |                or changed that does not require a new     |
 |         |                compile unless the new feature is required |
 |---------------------------------------------------------------------|
 | 21.1    | Minor change - Support for ULTS Galileo assistance data   |
 |---------------------------------------------------------------------|
 | 22      | Added support for GAGAN                                   |
 |---------------------------------------------------------------------|
 | 22.1    | Added support for SDCM                                    |
 |---------------------------------------------------------------------|
 | 22.2    | Added support for RTCM                                    |
 |---------------------------------------------------------------------|
 | 22.3    | Minor change - Added Transmit Group Delay parameter       |
 |---------------------------------------------------------------------|
 | 22.4    | Minor change - added bespoke signal structure, a subset   |
 |         |                of the original                            |
 |---------------------------------------------------------------------|
 | 22.5    | Minor change - changed Signal::prn to int, GLONASS has    |
 |         |                negative PRN numbers                       |
 |---------------------------------------------------------------------|
 | 23.00   | Major change - 06/11/2017 Added fourth frequency band to  |
 |         |                the Signal structure. The packet size has  |
 |         |                increased to allow for the additional      |
 |         |                frequency specific data. A major number    |
 |         |                means a re-compile is necessary. A minor   |
 |         |                change means that something has been added |
 |         |                or changed that does not require a new     |
 |         |                compile unless the new feature is required.|
 |         |                This change is classed as a major change.  |
 |         |                You will need to recompile your code with  |
 |         |                the latest header file and use the latest  |
 |         |                datastreaming.dll to work with SimGEN V6.03|
 |---------------------------------------------------------------------|
 | 24.00   | Major change - Added fifth frequency band. Also added     |
 |         |                a buffer to prevent the message size       |
 |         |                altering too often in future.              |
 |         |              - Support for ULTS Galileo assistance data   |
 |         |              - Reduced Message::max_signal to keep under  |
 |         |                Ethernet MTU size. PosApp will now         |
 |         |                broadcast 2 Signal Data structures         |
 |         |                per Message                                |
 |         |              - Increased Message::max_signal_compact for  |
 |         |                the same purpose                           |
 |---------------------------------------------------------------------|
 | 25.00   | Major change - Added multi-constellation ids, and         |
 |         |                removed legacy GPS multi-constellation     |
 |         |                Signal(Antenna::Type) and Frequency       |
 |         |                (Frequency::Type) enumerations.            |
 |         |              - Added new GTx, removed legacy BASE_STATION |
 |         |                and GSM.                                   |
 |         |              - Reordering of Signal and Frequency enums   |
 |         |                to be more logical and consistent          |
 |---------------------------------------------------------------------|
 | 25.1    | Minor change - Added definitions for Galileo and GLONASS  |
 |         |                codes and extended list of GPS codes       |
 |         |              - Added in WiFi and Traffic Control structs  |
 |         |                for PosiFi and OKTAL-SE.                   |
 |---------------------------------------------------------------------|
 | 25.2    | Minor change - Added definitions for new GLONASS CDMA     |
 |         |                frequencies. Added GTx generic             |
 |         |                constellation.                             |
 |---------------------------------------------------------------------|
 | 25.3    | Minor change - Added Galileo ionospheric parameters to    |
 |         |                the Keplerian struct, Kelperian data can   !
 |         |                sent for BeiDOU, Galileo, IRNSS and QZ     |
 |---------|-----------------------------------------------------------|
 | 26.0    | Major change - Added sixth frequency band to accommodate  |
 |         |                BeiDou B2b.                                !
 |---------------------------------------------------------------------|
 | 27.0    | Major change - time_into_run, time_of_validity 64 bit     |
 |         |                microseconds                               |
 |         |                simulation_update_rate microseconds        |
 |         |                UDP_output_rate_ms microseconds            |
 |         |                Name UDP_output_rate_ms -> UDP_output_rate |
 |         |                Truth_Aiding_EGI rate microseconds         |
 |         |                Move Signal::frequency_specific to end     |
 |         |                of Signal for ease of future compatibility |
 |         |                                                           |
 |---------|-----------------------------------------------------------|
 | 28.0    | Major change - Added Vehicle_id structure to header       |
 |         |                incorporating vehicle_id and vehicle_type_ |
 |         |                fields to support spoof vehicles.          |
 |---------|-----------------------------------------------------------|
 | 28.1    | Minor change - MNAV added icd_revision and frequency      |
 |---------|-----------------------------------------------------------|
*************************************************************************/

namespace EthernetData
{
    /*************************************************************************
     Static initialises
     If the major version changes then a re-compile is necessary since this
     means that the packet size has changed.
    *************************************************************************/
    static const unsigned int   tick_time       = 1000;
    static const unsigned short	version_major	= 28;
    static const unsigned short version_minor   = 1;
    static const unsigned int   port            = 15660;

    /*************************************************************************
     Max size of single message.
     Need to be maintained for backward compatibility(if possible).
    *************************************************************************/

    static const unsigned int   max_message_size = 1472; //bytes

    /*************************************************************************
     Vehicle Identifier
    *************************************************************************/
    struct Vehicle_id
    {
        enum Type
        {
            real_veh,   // the normal case
            spoof_veh   // for a spoof vehicle
        };

        unsigned int id;
        Type         type;
    };

    /*************************************************************************
     Vehicle Message Data
    *************************************************************************/
    struct Vehicle
    {
        Vehicle_id        vehicle_id;
        double            posn                            [ 3 ];
        double            velocity                        [ 3 ];
        double            acceleration                    [ 3 ];
        double            jerk                            [ 3 ];
        double            latitude;
        double            longitude;
        double            height;
        double            attitude                      [ 3 ];
        double            angular_velocity              [ 3 ];
        double            angular_acceleration          [ 3 ];
        double            angular_jerk                  [ 3 ];
        double            baro_height;
        double            ground_speed;
        double            velocity_ned                  [ 3 ];
        double            angular_velocity_inertial     [ 3 ];
        double            angular_acceleration_inertial [ 3 ];
        double            angular_jerk_inertial         [ 3 ];
    };

    /*************************************************************************
     Antenna Message Data
    *************************************************************************/
    struct Antenna
    {
        Vehicle_id        vehicle_id;
        unsigned int      antenna;
        unsigned int      spare;        // Pad to 8-byte boundary
        double            posn                          [ 3 ];
        double            velocity                      [ 3 ];
        double            acceleration                  [ 3 ];
        double            local_velocity_ned            [ 3 ];
        double            latitude;
        double            longitude;
        double            height;
        double            dop;

        // Note that these types match the types in
        // Signal_capabilities_defs.h

        enum Type
        {
            gps,
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
            max_type
        };

        static const int max_constellation_ids = 13;
        static const int flex_index = 12;  // this is array index of the "Flex" constellation

        unsigned int    num_signals_of_type [ Antenna::max_type ][ Antenna::max_constellation_ids ];

        unsigned int    spare1; // pad to 8-byte boundary!!

        // Enhanced spin parameters
        double          spin_attitude                   [ 3 ];
        double          spin_angvel_x_net;
        double          spin_cum_phase_krevs;
    };

    /*************************************************************************
     Frequency Specific Data
    *************************************************************************/
    struct Frequency
    {
        static const int max_freq = 6;

        enum Type
        {
            gps_l1, gps_l2, gps_l5,
            waas_f1, waas_l5,
            egnos_f1, egnos_l5,
            msas_f1, msas_l5,
            gagan_f1, gagan_l5,
            sdcm_l1,
            glonass_f1, glonass_f2,
            galileo_l1, galileo_e5, galileo_e6,
            qz_l1, qz_l2, qz_l5, qz_l6,
            irnss_s, irnss_l5,
            beidou_b1i,beidou_b2i, beidou_b2a, beidou_b1c, beidou_b3i, beidou_b2b,
            caps_c1, caps_c2, caps_c3, caps_c4,
            ground_tx_f1, ground_tx_f2, ground_tx_l5,
            gtx_gln_l1, gtx_gln_l2,
            gtx_beidou_b1i, gtx_beidou_b2i, gtx_beidou_b2a, gtx_beidou_b1c, gtx_beidou_b3i, gtx_beidou_b2b,
            gtx_galileo_e1, gtx_galileo_e5, gtx_galileo_e6, // Not E5a or E5b
            jammer_f1,
            laas_vdb,
            glonass_cdma_l1,
            glonass_cdma_l2,
            glonass_cdma_l3,
            gtx_generic_1,
            gtx_generic_2
        };

        enum Code
        {
            gps_ca_code,
            gps_p_code,
            gps_y_code,
            gps_m_code,
            gps_mspot,
            gps_mearth,
            gps_l1c,
            gps_l2c,
            gps_l5c,
            gps_m_noise,
            gps_pa,
            gps_sds,
            gps_pilot,
            gps_data,
            glon_ca_code,
            glon_p_code,
            gal_e1_a_code,
            gal_e1_b_code,
            gal_e1_c_code,
            gal_e5_ai_code,
            gal_e5_aq_code,
            gal_e5_bi_code,
            gal_e5_bq_code,
            gal_e6_a_code,
            gal_e6_b_code,
            gal_e6_c_code,
            qzss_ca_code,
            qzss_l1s_code,
            qzss_l1c_code,
            qzss_l2c_code,
            qzss_l5_code,
            qzss_l6_code,
            num_codes
        };

        Type            type;
        unsigned int    mc_id;
        unsigned int    spare; // pad to 8-byte boundary!!
        unsigned int    spare2; // pad to 8-byte boundary!!
        double          pseudorange;
        double          pseudorange_rate;
        double          pseudorange_error;
        double          delta_range;
        double          iono_delay;
        double          signal_level;
        unsigned int    code_types;     // composite of Code
        float           transmit_group_delay;
    };

    /*************************************************************************
     Signal Message Data
    *************************************************************************/
    struct Errors
    {
        double          divergence_terms;
        double          relativistic_effects;
        double          ramp_error;
        double          clock_noise;
        double          spare;
    };

    struct Signal
    {
        Vehicle_id      vehicle_id;
        unsigned int    antenna;
        unsigned int    channel;
        unsigned int    satellite;
        Antenna::Type   type;
        unsigned int    mc_id;
        unsigned int    spoofer_tx;
        unsigned int    prn;
        unsigned int    multi_path_index;
        unsigned int    num_of_frequencies;
        unsigned int    spare; // pad to 8-byte boundary!!
        double          posn                            [ 3 ];
        double          velocity                        [ 3 ];
        double          acceleration      [ 3 ];
        double          elevation;
        double          azimuth;
        double          true_range;
        double          true_range_rate;
        double          tropo_delay;
        double          antenna_elevation;
        double          antenna_azimuth;
        Errors          errors;
        Frequency       freqency_specific[Frequency::max_freq];
    };

    struct Signal_compact
    {
        unsigned short  vehicle_id;
        unsigned short  vehicle_type;
        unsigned int    satellite;
        unsigned char   sig_type;   // defines the signal type, as per EthernetData::Antenna::Type enum
        unsigned char   mc_id;      // and the multi-constellation id.
        unsigned short  freq_types; // bit map defining which frequency bands are enabled(0 = disabled, 1 = enabled)
                                    // Bit 0: L1
                                    // Bit 1: L2
                                    // Bit 2: L5
                                    // Bit 3: L6
                                    // Bit 4: LX
        unsigned int    number_of_multi_paths_associated;
        double          posn     [ 3 ];
        double          velocity [ 3 ];
    };

    /*************************************************************************
     Truth aiding at ins sensor offset
    *************************************************************************/
    struct Truth_Aiding
    {
        Vehicle_id      vehicle_id;
        unsigned int    body_valid;
        unsigned int    local_valid;
        double          latitude;
        double          longitude;
        double          height;
        double          velocity_body_inertial      [ 3 ];
        double          acceleration_body_inertial  [ 3 ];
        double          jerk_body_inertial          [ 3 ];
        double          velocity_local_inertial     [ 3 ];
        double          acceleration_local_inertial [ 3 ];
        double          jerk_local_inertial         [ 3 ];

        unsigned int    model_number;                   // Along with the vehicle this
                                                        // uniquely identifies the source
                                                        // of this data as there can be
                                                        // multiple INS models under a vehicle.
                                                        // Model starts from 1..n

        char            spare1[4]; // pad to 8-byte boundary
    };

    /*************************************************************************
     Truth aiding at ins sensor offset EGI data
    *************************************************************************/

    struct old_egi_aiding
    {
        double jerk_inertial[3];
    };

    struct new_egi_aiding
    {
        double            baro_height;
        double            spare[2];
    };

    struct Truth_Aiding_EGI
    {
        Vehicle_id        vehicle_id;
        unsigned int      rate; // microseconds
        unsigned int      spare; // Pad to 8-byte boundary
        double            acceleration_inertial [ 3 ];

        // Jerk data is no longer needed so this location can be used for baro data
        // However we need to retain the old format for SimEGI
        union
        {
            struct old_egi_aiding old_format;
            struct new_egi_aiding new_format;

        } alt_data;

        double            posn_ecef                      [ 3 ];
        double            velocity_ecef                  [ 3 ];
        double            acceleration_ecef              [ 3 ];
        double            jerk_ecef                      [ 3 ];
        double            angular_velocity_body_ecef     [ 3 ];
        double            angular_acceleration_body_ecef [ 3 ];
        double            angular_jerk_body_ecef         [ 3 ];
        double            attitude                       [ 3 ];

        unsigned int      model_number;                 // Along with the vehicle this
                                                        // uniquely identifies the source
                                                        // of this data as there can be
                                                        // multiple INS models under a vehicle.
                                                        // Model starts from 1..n
        char            spare1[4]; // pad to 8-byte boundary
    };

    /*************************************************************************
     INS aiding error model
    *************************************************************************/
    struct INS_Error_Model
    {
        enum Reference_frame
        {
            local = 0,
            body  = 1
        };

        Vehicle_id        vehicle_id;
        Reference_frame   reference_frame;
        unsigned int      spare; // Pad to 8-byte boundary
        double            attitude             [ 3 ];
        double            latitude;
        double            longitude;
        double            height;
        double            velocity             [ 3 ];
        double            acceleration         [ 3 ];
        double            angular_rate         [ 3 ];
        double            angular_acceleration [ 3 ];
        double            cnex                 [ 3 ];
        double            latitude_acceleration;
        double            longitude_acceleration;
        double            height_acceleration;
        double            baro_height;
        double            magnetic_heading;

        unsigned int      model_number;                   // Along with the vehicle this
                                                          // uniquely identifies the source
                                                          // of this data as there can be
                                                          // multiple INS models under a vehicle.

        unsigned int      spare1;                         // Pad to 8-byte boundary

        // Nav incl wander angle
        double            Nav_Velocity          [ 3 ];
        double            Platform_Azimuth;
        double            Nav_Acceleration      [ 3 ];
        double            NAV_cnex              [ 3 ];
        double            X_Axis_Residual_Tilt;
        double            Y_Axis_Residual_Tilt;

        // Local
        double            INS_Quaternion        [ 4 ];
        double            Heading_Rate;
        double            Pitch_Rate;
        double            Roll_Rate;

        double            msl_height_d;
    };

    /*************************************************************************
     Space vehicle
    *************************************************************************/
    struct Space_Vehicle
    {
        Vehicle_id        vehicle_id;
        double            thrust                 [ 3 ];
        double            latitude_rate;
        double            heading_rate;
        double            elevation_rate;
        double            bank_rate;
        double            heading_orb;
        double            elevation_orb;
        double            bank_orb;
        double            longtitude_rate_cos_lat;
    };


    /*************************************************************************
     IMU data - used by the Aiding Internal IMU
    *************************************************************************/
    struct IMU_data
    {
        enum Sensors
        {
            max_sensors = 6
        };

        Vehicle_id      vehicle_id;
        unsigned int    model_number;
        unsigned int    spare; // Pad to 8-byte boundary
        double          time_of_validity;
        double          delta_no_errors_velocity[ max_sensors ];
        double          delta_no_errors_theta   [ max_sensors ];
        double          delta_errored_velocity  [ max_sensors ];
        double          delta_errored_theta     [ max_sensors ];
    };

    /*************************************************************************
     SimSENSOR data packet
    *************************************************************************/
    struct SimSENSOR
    {
        enum Sensors
        {
            max_sensors = 3
        };

        struct Gyro
        {
            // Noise included
            double delta_theta[ max_sensors ];
            double avg_rate[ max_sensors ];
        };

        struct Accelerometer
        {
            // Noise included
            double delta_velocity[ max_sensors ];
            double avg_acceleration[ max_sensors ];
        };

        struct Magnetometer
        {
            // Body frame axis
            double magnetic_flux_density_uT[ 3 ];
        };

        struct Compass
        {
            double  magnetic_heading_rad;
            double  true_heading_rad;
            double  bank_rad;           // only used for three axis compass
            double  elevation_rad;      // only used for three axis compass
        };

        struct Barometer
        {
            double height_m;
        };

        Vehicle_id      vehicle_id;
        unsigned int    model_number;
        unsigned int    spare; // Pad to 8-byte boundary
        double          time_of_validity_s;

        Gyro            gyro;
        Accelerometer   accelerometer;
        Magnetometer    magnetometer;
        Compass         compass;
        Barometer       barometer;
    };

    /*************************************************************************
     Keplerian
    *************************************************************************/
    struct Almanac
    {
        int             time_of_almanac_s;
        int             week_number;
        double          eccentricity;
        double          deviation_in_inclination_angle_sc;
        double          rate_of_right_ascension_scps;
        double          square_root_sma_sqm;
        double          longitude_acending_node_sc;
        double          argument_of_perigee_sc;
        double          mean_anomaly_sc;
        double          af0_s;
        double          af1_sps;
    };

    struct Ephemeris
    {
        enum Amplitude_harmonic_index
        {
            cuc_rad = 0,
            cus_rad,
            crc_m,
            crs_m,
            cic_rad,
            cis_rad,
            max_cxx
        };

        int             time_of_ephemeris_s;
        int             iode;
        double          mean_anomaly_sc;
        double          mean_motion_difference_scps;
        double          eccentricity;
        double          square_root_sma_sqm;
        double          longitude_acending_node_sc;
        double          inclination_angle_sc;
        double          argument_of_perigee_sc;
        double          rate_of_right_asension_scps;
        double          rate_of_inclination_angle_scps;
        double          amplitude_harmonic_correction[ max_cxx ];
    };

    struct Iono_klobuchar
    {
        enum{ num_params = 4 };
        double alpha_coefficients[ num_params ];
        double beta_coefficients[ num_params ];
    };

    struct Iono_galileo
    {
        enum{ num_orders = 3 };   // 1st, 2nd and 3rd order parameters
        double effective_ionisation_level[ num_orders ];

        enum{ num_regions = 5 };
        bool storm_flags[ num_regions ];

        char spare[35]; // To make it same size as its union member Iono_klobuchar
    };

    union Ionospheric
    {
        Iono_klobuchar iono_klobuchar; // for Galileo, GPS,
        Iono_galileo iono_galileo;
    };

    struct Clock
    {
        double af0;
        double af1;
        double          af2;
        double          toc;
        int             iodc;
        int             spare;
    };

    /*************************************************************************
     Almanac, Ephemeris, Ionospheric & Clock data
    *************************************************************************/
    struct Keplerian
    {
        enum Generation
        {
            from_computed_data = 0,
            from_nav_data      = 1
        };

        Vehicle_id      vehicle_id;
        unsigned int    antenna;
        unsigned int    channel;
        unsigned int    satellite;
        unsigned int    prn;
        unsigned int    time_s;  // GPS time for GPS/QZ, Galileo time for Galileo etc
        unsigned int    tow_s;
        unsigned int    week_number; // includes rollover
        unsigned int    spare;       // Pad to 8-byte boundary
        Almanac         almanac;
        Ephemeris       ephemeris;
        Ionospheric     ionospheric;
        Clock           clock_terms;
        Generation      bit_mods;
        Antenna::Type   type;
        unsigned int    mc_id;

        char            spare1[4]; // Pad to 8-byte boundary
    };

    /*************************************************************************
     Legacy Nav Data
    *************************************************************************/
    struct LNav
    {
        Vehicle_id      vehicle_id;
        unsigned int    antenna;
        unsigned int    channel;
        unsigned int    satellite;
        unsigned int    prn;
        char            nav_bits[ 38 ]; // 300bits = 37.5 bytes
        char            spare[2]; // Pad to 4-byte boundary
        Antenna::Type   type;
        unsigned int    mc_id;
    };

    /*************************************************************************
     M Code Nav Data
    *************************************************************************/
    struct MNav
    {
        Vehicle_id       vehicle_id;
        unsigned int     antenna;
        unsigned int     channel;
        unsigned int     satellite;
        unsigned int     prn;
        char             nav_bits[ 50 ]; // 400bits        
        char             icd_revision;
        char             spare; // Pad to 4-byte boundary
        Antenna::Type    type;
        unsigned int     mc_id;
        Frequency::Type  frequency; //0 (L1) / 1 (L2)
    };

    /*************************************************************************
     Car Motion Sensor Data
    *************************************************************************/
    struct Car_Motion_Sensors
    {
        enum AnalogueCard
        {
            max_num_outputs = 4
        };

        Vehicle_id   vehicle_id;
        unsigned int odometer_enabled;
        unsigned int gyrorate_enable;            // aka heading rate gyro
        unsigned int ratetable_enable;           // aka rate table position (heading)
        unsigned int ratetable_speed_enable;
        unsigned int can_enabled;
        unsigned int gyrotemp_enable;
        unsigned int ratetable_north_align;
        unsigned int spare;                      // Pad to 8-byte boundary
        double       gyrorate_sens;              // volts / rad / sec about car vertical axis
        double       gyrorate_offset;            // voltage representing zero rate of change of heading
        double       ratetable_sens;             // volts / rad position about car vertical axis
        double       ratetable_offset;           // voltage representing zero heading
        double       ratetable_speed_sens;       // volts / rad / Sec about car vertical axis
        double       ratetable_speed_offset;     // voltage representing zero rotation rate
        double       gyrotemp_sens;              // volts / deg Celsius
        double       gyrotemp_offset;            // voltage representing zero degrees Celsius
        double       wheelbase;                  // distance between centers of fore and aft axles in meters
        double       wheeltrack;                 // distance between centers of contact patches of left and right wheels
        double       deadband_threshold;         // minimum speed in m/s for wheel sensor output
        double       ratetable_north_offset;     // offset in degrees of turntable zero position from true north
        double       cog_pulse_rate;             // pulses per km
        double       front_odo_rate;
        double       rear_odo_rate;
        double       fl_wheel_rate;
        double       fr_wheel_rate;
        double       rl_wheel_rate;
        double       rr_wheel_rate;
        unsigned int odos_all_same;            // flag set means use cog rate for all outputs

        unsigned int log_cms_data;
        double       log_cms_data_every_ms;

        // The temperature model is: temperature starts at and stays
        // at 'start_temperature' for 'seconds_at_start_temperature'
        // seconds, then ramps slowly to 'steady_state_temperature', taking
        // another 'seconds_to_ramp_temperature' to do so. Then it stays
        // at this steady state temperature for the rest of the run.
        double       start_temperature;
        double       steady_state_temperature;
        double       seconds_at_start_temperature;
        double       seconds_to_ramp_temperature;

        double       ana_sig_map[ max_num_outputs ];

        // Other useful information about the car motion
        double       turn_radius;
        double       cog_speed;
        double       cog_accel;
        double       latitude;
        double       longitude;
        double       height;
        unsigned int fwd_rev;
        unsigned int num_vehicles;

        // added 11/4/2012 - signal types mapped to each analog op, etc
        unsigned int    sig_type[ max_num_outputs ];
        unsigned int    user_files_enabled;         // Use user supplied files to drive analogue/digital PCI cards?

        unsigned int    spare1; // pad to 8-byte boundary

        double          max_ana_volts;
        double          min_ana_volts;
        double          left_right_gyrorate;
        double          spare2; // pad to 8-byte boundary
    };

    /*************************************************************************
     ULTS Reference Time
    *************************************************************************/
    enum { NUM_ASSIST_BLOCKS = 16 };
    struct ULTS_Ref_Time
    {
        double       GPS_tow;
        unsigned int GPS_week;
        unsigned int num_sats;
        unsigned int GPS_tow_assist[NUM_ASSIST_BLOCKS];
    };


    /*************************************************************************
     DGPS corrections
    *************************************************************************/
    struct ULTS_DGPS_Corrections
    {
        int                GPS_tow;               // GPS time of week in s. (0 to 604799)
        int                udre_status;           // Status/Health value
        int                number_of_satellites;  // The number of satellites(N_SAT)
        int                spare; // pad to 8-byte boundary
        struct sat_data
        {
            float          pr_corr;               // pseudorange correction(m)
            float          prr_corr;              // pseudorange correction rate (m/s)
            unsigned char  sat_id;                // The Satellite identifier(0-63)
            unsigned char  iode;                  // Issue of Data ephemeris
            unsigned char  udre;                  // User Differential Range Error
            char           spare;                 // pad to 8-byte boundary
        };
        sat_data sat_corr_data[NUM_ASSIST_BLOCKS];
    };

    /*************************************************************************
     Navigation model
    *************************************************************************/
    struct ULTS_Nav_Model
    {
        unsigned int    gps_time;                 // The GPS time (s) from which this data applies
        char            total_num_of_blocks;      // No of satellites included (1 to 16)
        char            block_number;             // Block number in sequence (1 to 16)
        char            sat_id;                   // Satellite id(PRN) (1 to 64)
        char            visible;                  // (For future expansion, always = 1 at present)
        char            data_bits[ 69 ];          // Block of Navigation model data
        char            spare[3];                 // pad to 8-byte boundary
    };

    /*************************************************************************
     Ionospheric model
    *************************************************************************/
    struct ULTS_Iono_Model
    {
        char coeffs[8];
    };

    /*************************************************************************
     UTC model
    *************************************************************************/
    struct ULTS_UTC_Data
    {
        char            params[13];
        char            spare[3];               // pad to 8-byte boundary
    };

    /*************************************************************************
     Almanac
    *************************************************************************/
    struct ULTS_Almanac
    {
        int             num_sats;              // The number of satellites included in this block
        int             week_number;           // The week number for the almanac data
        char            alm_data[ 188 ];       // Almanac data for up to 8 satellites
        char            num_blocks;            // The number of consecutive blocks of this type
        char            curr_block;            // The number of the current block(1 to n)
        char            sat_id[ 8 ];           // Satellites in this message(PRN-1)
        char            spare[2];              // pad to 8-byte boundary
    };

    /*************************************************************************
     Acquisition assistance
    *************************************************************************/
    struct ULTS_Acq_Assist
    {
        unsigned int    number_of_sats;
        float           GPS_tow;
        struct sat_assist
        {
            unsigned short int code_phase;
            short int          scaled_doppler_0;
            char               scaled_doppler_1;
            unsigned char      scaled_azimuth;
            unsigned char      scaled_elevation;
            unsigned char      scaled_doppler_uncertainty;
            char                 integer_code_phase;
            char                 GPS_bit_num;
            char                 svid;
            char                 spare;        // pad to 8-byte boundary
        } sat_data[NUM_ASSIST_BLOCKS];         // Data for up to 16 satellites
    };

    /*************************************************************************
     Real-time integrity - GPS
    *************************************************************************/
    struct ULTS_Integrity
    {
        unsigned char    num_bad_sats;
        unsigned char    bad_sat_id[NUM_ASSIST_BLOCKS];
        char             spare[7];              // pad to 8-byte boundary
    };

    /*************************************************************************
     Data server channel update
    *************************************************************************/
    struct DS_channel_update_data
    {
        unsigned short int svid;       // 0 in the svid field denotes channel is STOPPING, > 0 = SVID
        unsigned short int channel;
        char               spare[4];   // pad to 8-byte boundary
        double request_time;           // time to request schedule update for
        double start_time;             // actual start time
    };

    /*************************************************************************
     Sync
    *************************************************************************/
    struct UTC_params
    {
        double A0;
        double A1;
        int    t_ot;
        int    wn_t;
        int    wn_lsf;
        int    dn;
        int    delta_t_lsf;
        int    delta_t_ls;
    };

    struct Sync
    {
        char            start_time          [ 25 ];
        char            spare               [  7 ]; // pad to 8-byte boundary
        unsigned int    duration_seconds;
        int             utc_offset_seconds;
        unsigned int    GPS_time_s;
        unsigned int    GPS_week;
        unsigned int    GPS_tow_s;
        unsigned int    spare2;                     // pad to 8-byte boundary
        UTC_params      utc_params;
    };

    /*************************************************************************
     Status change message
    *************************************************************************/
    struct Status
    {
        enum Type
        {
            no_scenario_loaded  = 0,
            loading             = 1,
            ready               = 2,
            arming              = 3,
            armed               = 4,
            running             = 5,
            paused              = 6,
            ended               = 7
        };

        enum Serial_poll_byte_bits
        {
            scenario_is_incomplete = 16,
            incompatible_hardware  = 32,
            srq                    = 64,           // Not used, SimGEN software will never raise an SRQ
            spare                  = 128
        };

        Type            status;
        int             simulation_update_rate;    // microseconds
        int             UDP_output_rate;           // microseconds
        unsigned int    serial_poll_byte;          // Upper 4 bits of least significant byte defined in Serial_poll_byte_bits, lower 4 bits will match status above
    };

    /*************************************************************************
     ULTS GANSS Reference Time(A.33)
    *************************************************************************/
    namespace ULTS_ganss_messages
    {
        enum{ MAX_NUM_SATS = 16 };
        enum{ MAX_NUM_NAV_MODEL_MSGS = 3 };
        enum{ MAX_NUM_ALM_MSGS = 4 };
        enum{ MAX_NUM_BDS_NAV_MODEL_MSGS = 2 };
        enum{ MAX_NUM_GAL_NAV_MODEL_MSGS = 2 };
    }

    struct ULTS_GANSS_Ref_Time
    {
        unsigned int ganss_day;
        unsigned int ganss_tod;
        unsigned int ganss_tod_uncertainty;
        unsigned int ganss_time_id;
    };

    /*************************************************************************
     ULTS GANSS Time Model(A.41)
    *************************************************************************/
    struct ULTS_GANSS_Time_Model
    {
        unsigned int ganss_id;
        unsigned int ganss_ref_time;
        double       T_A0;
        double       T_A1;
        double       T_A2;
        unsigned int gnss_to_id;
        unsigned int week_num;
    };

    /*************************************************************************
     ULTS GANSS Differential Corrections(A.43)
    *************************************************************************/
    // Note that the data is NOT scaled in this structure!

    struct Glon_corr
    {
        float pr_corr;
        float prr_corr;
    };

    struct Beidou_corr
    {
        float		    deltaT;         // ECC Equivalent clock correction data
                                        // as broadcast in the D2 nav data by BeiDou GEO sats, LSB = 0.1 metres
        unsigned char	rurai;          // Regional User Range Accuracy(table)
        char            spare[3];       // pad to 8-byte boundary
    };

    struct Galileo_corr
    {
        float pr_corr;
        float prr_corr;
    };

    struct ULTS_DGANSS_Corrections
    {
        unsigned int	ganss_id;
        unsigned int	dganss_ref_time;        // For BeiDou time of corrections in seconds into hour(BeiDou time)
        unsigned char	total_num_of_blocks;	// number of signals(max = 8)
        unsigned char   block_number;
        unsigned char   ganss_signal_id;        // Table A.59
        unsigned char	status_health;
        unsigned int	num_sats;

        struct Sat_data
        {
            unsigned char	sat_id;             // for both GLONASS, BeiDou
            unsigned char	udre;               // User Differential Range Error, table,
                                                // for both GLONASS, BeiDou
            char            spare[2];           // pad to 8-byte boundary
            unsigned int	iod;                // GLONASS only
            union
            {
                Glon_corr    glon_corr;
                Beidou_corr  beidou_corr;
                Galileo_corr galileo_corr;
            } correction;


        } sat_corr_data[ULTS_ganss_messages::MAX_NUM_SATS];
    };

    /*************************************************************************
    ULTS GANSS Navigation Model Contents(GLONASS) (A.46)
    *************************************************************************/
    struct ULTS_GLONASS_Nav_Model
    {
        unsigned int  ganss_id;
        unsigned char nbi_flag;                   //default 0
        unsigned char num_sats;
        unsigned char total_num_of_blocks;
        unsigned char block_number;

        struct Sat_data
        {
            unsigned char   sat_id;
            unsigned char   sv_health;	            // B_n, F_T(A.48.1)
            char            spare[6];               // pad to 8-byte boundary
            unsigned int    iod;		            // t_b(A.48.2)
            unsigned char   E_n;
            unsigned char   P_1;
            unsigned char   P_2;
            unsigned char   m_type;
            double		    tau_n;	                // Table A.49, ICD 4.5
            double		    gamma_n;
            double		    delta_tau_n;
            double		    pos_x_n;
            double		    vel_x_n;
            double		    acc_x_n;
            double		    pos_y_n;
            double		    vel_y_n;
            double			acc_y_n;
            double			pos_z_n;
            double			vel_z_n;
            double			acc_z_n;
        } sat_data[ULTS_ganss_messages::MAX_NUM_NAV_MODEL_MSGS];
    };

    /*************************************************************************
    ULTS GANSS Real-Time Integrity(A.50)
    *************************************************************************/
    struct ULTS_GANSS_Integrity
    {
        unsigned int     ganss_id;
        unsigned int     num_bad_sats;

        struct Bad_sats
        {
            unsigned int   sat_id;
            unsigned int   signal_id;      //bit per signal, Table A.59(up to 8 signals)

        }bad_sats[ULTS_ganss_messages::MAX_NUM_SATS];
    };

    /*************************************************************************
    GANSS Reference Measurement Information(A.52)  a.k.a. acquisition assistance
    *************************************************************************/
    // !!!! NOTE that in this structure the data IS scaled !!!!!

    struct Az_el_glo    // This is what is used for GLONASS
    {
        unsigned char	scaled_azimuth;         // degrees, (scale 11.5 deg)
        unsigned char	scaled_elevation;       // degrees, (scale 11.5 deg)
        char            spare[2];               // pad to 8-byte boundary
    };

    struct Az_el_bdu    // This is how I read the 3GPP change note for BeiDou
    {
        short int scaled_azimuth;         // 0 to 360 degrees, (scale 0.703125)
        short int scaled_elevation;       // degrees, (scale 0.703125)
    };

    struct ULTS_GANSS_Ref_Info
    {
        unsigned int    ganss_id;           // enumerated SBAS, Modernised GPS, QZ, GLONASS, BDS(beidou)
        unsigned char   ganss_signal_id;    // i.e. frequency, sig type, e.g. GPS L1C, GLONASS G1, Beidou B1l
        unsigned char   num_sats;
        char            spare[2];           // pad to 8-byte boundary

        struct Sat_data
        {
            unsigned int  scaled_code_phase;          // scale 2^-10 milliseconds
            int           scaled_doppler_0;           // scale 0.5 m/s
            unsigned char sat_id;
            char          scaled_doppler_1;           // scale factor 1/210 m/s/s
            unsigned char scaled_doppler_uncertainty; // table of values
            unsigned char scaled_int_code_phase;      // ms units, modulo 128 ms
            union
            {
                Az_el_glo az_el_glo;
                Az_el_bdu az_el_bdu;
            }az_el;

        } sat_data[ULTS_ganss_messages::MAX_NUM_SATS];
    };

    /*************************************************************************
    7)	GLONASS Almanac Model (A.54)

    NOTE: NO SVID IN THE ALMANAC ARRAY
    So we must assume elements contain data for sats 1 to 24 in order(6 messages in a block,
    4 satellites per message)
    *************************************************************************/
    struct ULTS_GLONASS_Alm_Model
    {
        unsigned int  ganss_id;
        unsigned int  num_sats;
        unsigned char week_num;
        unsigned char almanac_ref_time;
        unsigned char almanac_iod;
        unsigned char block_number;
        unsigned int  total_num_of_blocks;

        struct Almanac_data                 // Table 4.9 ICD
        {
            unsigned int  N_A;
            unsigned char n_A;
            unsigned char H_n_A;
            unsigned char C_n_A;
            unsigned char M_n_A;
            double        lambda_n_A;
            double        t_lambda_n_A;
            double        delta_i_n_A;
            double        delta_T_n_A;
            double        delta_T_dot_T_n_A;
            double      	    e_n_A;
            double		        omega_n_A;
            double		        tau_n_A;
            unsigned char		signals_available;              // Added from 3GPP Table A.55.19
            unsigned char		spare[7];                       // pad to 8-byte boundary

       } almanac_data[ULTS_ganss_messages::MAX_NUM_ALM_MSGS];
    };

    /*************************************************************************
    Rtcm message information
    *************************************************************************/
    struct Rtcm_data
    {
        unsigned int size; 
        char         data[ 1000 ];
        unsigned int total_packets;
        unsigned int current_packet;
        char         spare[4];          // pad to 8-byte boundary
    };

    struct Galileo_alm_data
    {
        unsigned int    svid;
        unsigned char   spare1[4];          // pad to 8-byte boundary!!
        double          root_A;             // Square root of semi-major axis M^1/2(scale 2^-11)
        double          e;                  // Eccentricity(scale 2^-21)
        double          arg_per;            // W, Argument of perigee, semi-circles(scale 2^-23)
        double        mean_anom;          // MO, Mean anomaly at ref time, semi-circles (scale 2^-23)
        double        long_asc_node;      // Omega0, Longitude of ascending node at ref time,  semi-circles (scale 2^-23)
        double        right_asc_rate;     // OmegaDot, Rate of right ascension, semi-circles/sec (scale 2^-38)
        double        inclin_corr;        // DeltaI, Correction of orbital reference inclination at ref time, semi-circles/sec (scale 2^-19)
        double        a0;                 // Sat clock bias, seconds (scale 2^-20)
        double          a1;                 // sat clock rate, secs/sec(scale 2^-38
        unsigned int    SV_health;          // GNSS to svHealth Bit String(8) relation
        unsigned char   spare[4];		    // pad to 8-byte boundary!!
    };

     struct ULTS_Galileo_Alm_Model
    {
        unsigned int  ganss_id;
        unsigned int  num_sats;
        unsigned int  week_num;           // Almanac reference week , number of weeks since the beginning of GANSS specific system time (mod 256).
        unsigned int  Toa;                // Almanac Reference Time common to all satellites in GANSS Almanac using Keplerian Parameters given in GNSS specific system time
        unsigned char ioda;               // Required for Galileo
        unsigned char complete_almanac;   // set = almanacs for complete constellation
        unsigned char block_number;       // multiple messages for a complete set of alms
        unsigned char total_num_blocks;   // num messages required
        unsigned char spare[4];           // pad to 8-byte boundary!!

        Galileo_alm_data gal_alm_data[ULTS_ganss_messages::MAX_NUM_ALM_MSGS];  // Max to fit within max message size!!!
    };

    /*************************************************************************
    BeiDou Almanac Model
    *************************************************************************/
    // NOTE parameters are NOT scaled in this message - scale factors are
    // those in the 3GPP spec, just given for information here

    struct Beidou_alm_data
    {
        unsigned int  svid;
        unsigned int  Toa;                // Almanac Reference Time. Time into Beidou week, seconds (scale 2^12)
        double        root_A;             // Square root of semi-major axis M^1/2 (scale 2^-11)
        double        e;                  // Eccentricity (scale 2^-21)
        double        arg_per;            // W, Argument of perigee, semi-circles (scale 2^-23)
        double        mean_anom;          // MO, Mean anomaly at ref time, semi-circles (scale 2^-23)
        double        long_asc_node;      // Omega0, Longitude of ascending node at ref time,  semi-circles (scale 2^-23)
        double        right_asc_rate;     // OmegaDot, Rate of right ascension, semi-circles/sec (scale 2^-38)
        double        inclin_corr;        // DeltaI, Correction of orbital reference inclination at ref time, semi-circles/sec (scale 2^-19)
        double        a0;                 // Sat clock bias, seconds (scale 2^-20)
        double        a1;                 // sat clock rate, secs/sec (scale 2^-38
        unsigned int  SV_health;          // Beidou ICD Table 5-15. All 0 = OK
        unsigned char spare[4];           // pad to 8-byte boundary!!

    };

    struct ULTS_Beidou_Alm_Model
    {
        unsigned int  ganss_id;
        unsigned int  num_sats;
        unsigned int  week_num;           // Beidou week number modulo 256
        unsigned int  Toa;                // Almanac Reference Time. Time into Beidou week, seconds (scale 2^12)
        unsigned char ioda;               // "Required for Galileo" - so not required for BeiDou?
        unsigned char complete_almanac;   // set = almanacs for complete constellation
        unsigned char block_number;       // multiple messages for a complete set of alms
        unsigned char total_num_blocks;   // num messages required
        unsigned char spare[4];           // pad to 8-byte boundary!!

        Beidou_alm_data bd_alm_data[ULTS_ganss_messages::MAX_NUM_ALM_MSGS];  // Max to fit within max message size!!!
    };

    /*************************************************************************
    BeiDou Nav Data Model(Ephemeris and clock corrections)

    NOTE parameters are NOT scaled in this message - scale factors are
         those in the 3GPP spec, just given for information here
    *************************************************************************/
    struct Beidou_sat_data
    {
        // general sat data
        unsigned char 	sat_id;
        unsigned char  	sv_health;	    // 0 = good, 1 = bad
        char            spare1[2];       // pad to 4-byte boundary
        unsigned int  	iod;
        char          	spare[2];
        char          	spare3[2];

        // BDS-ClockModel
        unsigned int  	Toc;	        // Time of clock(seconds) (scale 2^3)  I am taking this to be the time at which the clock
                                        // corrections apply(i.e. simulation start time) expressed as Time into Beidou week(secs)
        unsigned int    spare2;
        unsigned int    spare4;         // pad to 8-byte boundary
        double  		A0;	            // Clock corr A0 coeff(scale 2^-33 secs)
        double	  	    A1;	            // Clock corr A1 coeff(scale 2^-50 secs/s)
        double	  	    A2;	            // Clock corr A2 coeff(scale 2^-66 secs/s/s)
        double		    Tgd1;	        // group delay(seconds) (scale factor 0.1nanosecs)

        //OrbitModel bds-KeplerianSet
        unsigned int	urai;		    // bdsURAI range 0 to 15
        unsigned int	Toe;		    // bdsToe Ephemeris ref time(seconds into BeiDou week)
        double		    root_a;	        // bdsAPowerHalf metres^1/2(scale 2^-19)
        double		    eccentricity;   // bdsE(scale 2^-33)
        double		    arg_perigee;    // bdsW semi-circles(scale 2^-31)
        double		    deltaN;	        // bdsDeltaN, mean motion diff semi-c/sec(scale 2^-43)
        double 		    mean_anom;		// bdsM0 Mean anomaly at ref time semi-c(scale 2^-31)
        double		    omega0;         // bdsOmega0, long asc node of orbital plane computed according to ref time semi_c(scale 2^-31)
        double 		    omegadot;       // bdsOmegaDot, rate of right asc. semi-c/sec(scale 2^-43
        double 		    IO;             // bdsI0, inclination angle at ref time semi_c(scale 2^-31)
        double		    Idot;           // bdsIDot, rate of inclination semi-c/sec(scale 2^-43)
        double		    Cuc;            // cos. harmonic corr. to latitude radians(scale 2^-31)
        double 		    Cus;            // sine harmonic corr. to latitude radians(scale 2^-31)
        double		    Crc;            // cos. harmonic corr. to orbit radius metres(scale 2^-6)
        double		    Crs;            // sine harmonic corr. to orbit radius metres(scale 2^-6)
        double		    Cic;            // cos. harmonic corr. to inclination radians(scale 2^-31)
        double 		    Cis;            // sine harmonic corr. to inclination radians(scale 2^-31)
    };

    struct ULTS_Beidou_Nav_Model
    {
        unsigned int    ganss_id;
        unsigned char   num_sats;
        unsigned char   block_number;
        unsigned char   total_num_of_blocks;
        unsigned char   spare[1];       // pad to 8-byte boundary

        Beidou_sat_data sat_data[ULTS_ganss_messages::MAX_NUM_BDS_NAV_MODEL_MSGS];
    };

    struct Galileo_sat_data
    {
        // general sat data
        unsigned char sat_id;
        unsigned char sv_health;    // 0 = good, 1 = bad
        unsigned char spare1[2];    // pad to 4-byte boundary
        unsigned int iod;
        unsigned char stanModelID;  // 0 == I/Nav(E1, E5b), 1 == F/Nav(E1, E5a)
        unsigned char sisa;         // SISA range 0 to 255

        unsigned char spare2[2];    // pad to 4-byte boundary

        // Galileo-Clock Model
        unsigned int Toc;       // Time of clock(seconds) (scale 2^3)  I am taking this to be the time at which the clock
                                // corrections apply(i.e. simulation start time) expressed as Time into Beidou week(secs)
        unsigned int spare[2];  // pad to 8-byte boundary
        double A0;              // Clock corr A0 coeff(scale 2^-33 secs)
        double A1;              // Clock corr A1 coeff(scale 2^-50 secs/s)
        double A2;              // Clock corr A2 coeff(scale 2^-66 secs/s/s)
        double Tgd1;            // group delay(seconds) (scale factor 0.1nanosecs)

        //Orbit Model gal-KeplerianSet

        unsigned int Toe;       // Toe Ephemeris ref time(seconds into BeiDou week)

        unsigned int spare3;    // pad to 8-byte boundary

        double root_a;          // APowerHalf metres^1/2(scale 2^-19)
        double eccentricity;    // E(scale 2^-33)
        double arg_perigee;     // W semi-circles(scale 2^-31)
        double deltaN;          // DeltaN, mean motion diff semi-c/sec(scale 2^-43)
        double mean_anom;       // M0 Mean anomaly at ref time semi-c(scale 2^-31)
        double omega0;          // Omega0, long asc node of orbital plane computed according to ref time semi_c(scale 2^-31)
        double omegadot;        // OmegaDot, rate of right asc. semi-c/sec(scale 2^-43
        double IO;              // I0, inclination angle at ref time semi_c(scale 2^-31)
        double Idot;            // IDot, rate of inclination semi-c/sec(scale 2^-43)
        double Cuc;             // cos. harmonic corr. to latitude radians(scale 2^-31)
        double Cus;             // sine harmonic corr. to latitude radians(scale 2^-31)
        double Crc;             // cos. harmonic corr. to orbit radius metres(scale 2^-6)
        double Crs;             // sine harmonic corr. to orbit radius metres(scale 2^-6)
        double Cic;             // cos. harmonic corr. to inclination radians(scale 2^-31)
        double Cis;             // sine harmonic corr. to inclination radians(scale 2^-31)
    };

    struct ULTS_Galileo_Nav_Model
    {
        unsigned int  ganss_id;
        unsigned char num_sats;
        unsigned char block_number;
        unsigned char total_num_of_blocks;
        unsigned char spare[1];       // Spare/To align to 8-byte boundary

        Galileo_sat_data sat_data[ULTS_ganss_messages::MAX_NUM_GAL_NAV_MODEL_MSGS];
    };

    struct Traffic_object
    {
        unsigned int uid;
        unsigned int spare;
        double latitude;
        double longitude;
        double height;
        double heading;
        double elevation;
        double bank;
        double speed;
    };

    struct Traffic_report
    {
        enum
        {
            max_traffic_objects = 20
        };

        unsigned int used;
        unsigned int spare;
        Traffic_object objects[max_traffic_objects];
    };

    struct Traffic_control
    {
        enum
        {
            max_traffic_objects = 20
        };

        unsigned int used;
        unsigned int spare;
        Traffic_object objects[max_traffic_objects];
    };

    struct Wifi_ap
    {
        uint64_t mac_address;
        unsigned int multipath_id;
        unsigned int spare;
        double frequency;
        double tx_power;
        double rx_power;
        double range;
    };

    struct  Wifi_ap_control
    {
        enum
        {
            max_wifi_aps = 20
        };

        unsigned int used;
        unsigned int spare;
        Wifi_ap wifi_aps[max_wifi_aps];
    };

    /*************************************************************************
     Ethernet Message Structure
    *************************************************************************/
    struct Message
    {
        /*************************************************************************
         Type of Messages that can exist
        *************************************************************************/
        enum Type
        {
            vehicle                 = 0,
            antenna                 = 1,
            signal_data             = 2,
            truth_aiding            = 4,
            truth_aiding_egi        = 5,
            ins_error_model         = 6,
            space_vehicle           = 7,
            status                  = 8,
            m_nav_data              = 9,
            legacy_nav_data         = 10,
            keplerian_data          = 11,
            imu_data                = 12,
            car_motion_sensor_data  = 13,
            ds_channel_update       = 14,
            simsensor_data          = 15,
            rtcm                    = 16,

            ults_beidou_alm_model   = 19,
            ults_beidou_nav_model   = 20,
            ults_ref_time           = 21,
            ults_dgps_corrections   = 22,
            ults_nav_model          = 23,
            ults_iono_model         = 24,
            ults_utc_data           = 25,
            ults_almanac            = 26,
            ults_acq_assistance     = 27,
            ults_integrity          = 28,

            ults_ganss_ref_time     = 29,
            ults_ganss_time_model   = 30,
            ults_dganss_corrections = 31,
            ults_glonass_nav_model  = 32,
            ults_ganss_integrity    = 33,
            ults_ganss_ref_info     = 34,
            ults_glonass_alm_model  = 35,
            ults_galileo_alm_model  = 37,
            ults_galileo_nav_model  = 38,
            signal_data_compact     = 39,

            wifi_ap_control         = 40,
            traffic_report          = 41,
            traffic_control         = 42,

            // Sync ID should always be 36 for backward compatibility.
            sync                    = 36,

            // This must be set to the highest type value
            max_types_idx           = traffic_control
        };

        enum
        {
            max_signal         = 2,
            max_signal_compact = 21
        };

        /*************************************************************************
         Message Data
        *************************************************************************/
        unsigned int                type;
        unsigned short              version_major;
        unsigned short              version_minor;
        long long                   time_into_run;      // microseconds
        long long                   time_of_validity;   // microseconds

        union
        {
            Vehicle                 vehicle;
            Antenna                 antenna;
            Signal                  signal[ max_signal ];                 // A zero in the vehicle id will indicate an empty signal
            Signal_compact          signal_compact[ max_signal_compact ]; // A zero in the vehicle id will indicate an empty signal
            Truth_Aiding            truth_aiding;
            Truth_Aiding_EGI        truth_aiding_egi;
            INS_Error_Model         ins_error_model;
            Space_Vehicle           space_vehicle;
            Status                  status_info;
            MNav                    m_nav;
            LNav                    legacy_nav;
            IMU_data                imu;
            Keplerian               keplerian;
            Car_Motion_Sensors      cms;
            SimSENSOR               simsensor;
            Rtcm_data               rtcm;

            ULTS_Ref_Time           ults_ref_time;
            ULTS_DGPS_Corrections   dgps_corrections;
            ULTS_Nav_Model          nav_model;
            ULTS_Iono_Model         iono_model;
            ULTS_UTC_Data           utc_data;
            ULTS_Almanac            almanac;
            ULTS_Acq_Assist         acquis_assist;
            ULTS_Integrity          real_time_integrity;

            ULTS_GANSS_Ref_Time     ults_ganss_ref_time;
            ULTS_GANSS_Time_Model   ults_ganss_time_model;
            ULTS_DGANSS_Corrections ults_dganss_corrections;
            ULTS_GLONASS_Nav_Model  ults_glonass_nav_model;
            ULTS_GANSS_Integrity    ults_ganss_integrity;
            ULTS_GANSS_Ref_Info     ults_ganss_ref_info;
            ULTS_GLONASS_Alm_Model  ults_glonass_alm_model;
            DS_channel_update_data  ds_channel_update_data;
            ULTS_Beidou_Alm_Model   ults_beidou_alm_model;
            ULTS_Beidou_Nav_Model   ults_beidou_nav_model;

            ULTS_Galileo_Alm_Model   ults_galileo_alm_model;
            ULTS_Galileo_Nav_Model   ults_galileo_nav_model;

            Wifi_ap_control          wifi_ap_control;
            Traffic_control          traffic_control;
            Traffic_report           traffic_report;

            Sync                    sync_info;
        } data;

        char                        spare[104];
    };

    /*****************************************************************
     This enum defines when you want the callback to happen. I.E. do
     you want the callback at the time of validity of the message or
     when the message arrives
    *****************************************************************/
    enum DeliveryType { tov, now };

    /*****************************************************************
     This is the callback type. It is expected that after the
     callback has been called the memory for the message will be
     freed. Note that the callback must not block or the Ethernet
     controller will not be able to receive any new messages.
     You can use data_p to hold any data you wish to be delivered to
     the callback when a message of that type is received.
    *****************************************************************/
    typedef void (*Callback_p)( EthernetData::Message*    msg_p,
                                void*                  data_p );

    /*****************************************************************
     Function prototypes for DLL binding
    *****************************************************************/
    typedef void        ( *ethernet_version_func          ) ( unsigned short*             major,
                                                              unsigned short*             minor          );
    typedef const char* ( *ethernet_software_version_func ) ( void                                       );
    typedef void        ( *ethernet_start_func            ) ( bool                        multi_threaded );
    typedef void        ( *ethernet_start_memory          ) ( bool                        multi_threaded );
    typedef void        ( *ethernet_start_winpcap         ) ( bool                        multi_threaded );
    typedef void        ( *ethernet_stop_func             ) ( void                                       );
    typedef void        ( *ethernet_register_func         ) ( EthernetData::Message::Type type,
                                                              EthernetData::Callback_p    callback_p,
                                                              EthernetData::DeliveryType  delivery_type,
                                                              void*                       client_data_p  );

    /*****************************************************************
     Function names for DLL binding
    *****************************************************************/
    static const char ethernet_dll[]              = "simgendatastreaming.dll";
    static const char software_version_dll_func[] = "ethernet_software_version";
    static const char version_dll_func[]          = "ethernet_version";
    static const char start_dll_func[]            = "ethernet_start";
    static const char start_dll_memory_func[]     = "ethernet_start_memory";
    static const char start_dll_winpcap_func[]    = "ethernet_start_winpcap";
    static const char stop_dll_func[]             = "ethernet_stop";
    static const char register_dll_func[]         = "ethernet_registerCB";

    /*****************************************************************
     Sender and Receiver buffer size settings
    *****************************************************************/
    static const unsigned int buffers      = 1024;
    static const unsigned int send_size    = ( buffers * sizeof( EthernetData::Message ));
    static const unsigned int receive_size = send_size;
}
#pragma pack( pop, ETHERNET_SHARE_H1 )
#endif // ETHERNET_SHARE_H
/*************************************************************************
 End Of File
*************************************************************************/
