#include <iostream>
#include <stdexcept>
#include <cerrno>
#include <cassert>
#include "navrnx.hpp"

using ngpt::NavDataFrame;

/// GPS:                   : Time of Clock in GPS time
///             data__[0]  : SV clock bias in seconds
///             data__[1]  : SV clock drift in m/sec
///             data__[2]  : SV clock drift rate in m/sec^2
///             data__[3]  : IODE Issue of Data, Ephemeris
///             data__[4]  : Crs (meters)
///             data__[5]  : Deltan (radians/sec)
///             data__[6]  : M0 (radians)                                ---- 1
///             data__[7]  : Cuc (radians)
///             data__[8]  : e Eccentricity
///             data__[9]  : Cus (radians)
///             data__[10] : sqrt(A) (sqrt(m))                           ---- 2
///             data__[11] : Toe Time of Ephemeris (sec of GPS week)
///             data__[12] : Cic (radians)
///             data__[13] : OMEGA0 (radians)
///             data__[14] : Cis (radians)                               ---- 3
///             data__[15] : i0 (radians)
///             data__[16] : Crc (meters)
///             data__[17] : omega (radians)
///             data__[18] : OMEGADOT (radians/sec)                      ---- 4
///             data__[19] : IDOT (radians/sec)
///             data__[20] : Codes on L2 channel
///             data__[21] : GPS Week (to go with TOE)
///             data__[22] : L2 P data flag                              ---- 5
///             data__[23] : SV accuracy (meters)
///             data__[24] : SV health (bits 17-22 w 3 sf 1)
///             data__[25] : TGD (seconds)
///             data__[26] : IODC Issue of Data, Clock                   ---- 6
///             data__[27] : Transmission time of message
///             data__[28] : Fit Interval in hours
///             data__[29] : empty
///             data__[30] : empty                                       ---- 7

/// WGS 84 value of the earth's gravitational constant for GPS user
constexpr double mi_gps {3.986005e14};

/// WGS 84 value of the earth's rotation rate
constexpr double OMEGAE_dot {7.2921151467e-5};

/// PZ-90/GLO mean angular velocity of the Earth relative to vernal equinox
constexpr double OMEGA_E {7.2921151467e-5}; // units: rad/sec

/// Seconds in (GPS) week
constexpr double SEC_IN_WEEK {604800e0};

/// Constant F for SV Clock Correction in seconds/sqrt(meters)
constexpr double F_CLOCK {-4.442807633e-10};

/// @brief get SV coordinates (WGS84) from navigation block
/// 
/// Compute the ECEF coordinates of position for the phase center of the SVs' 
/// antennas. The time parameter should be given in GPS Time
/// @param[in] toe_sec  Time of Ephemeris as seconds in day
/// @param[in] t_sec    Epoch as seconds in day
/// @param[out] state SV x,y,z -components of antenna phase center position in 
///             the WGS84 ECEF coordinate system in meters; the state array must
///             have length >=3
/// @return Anything other than 0 denotes an error
///
/// @note Input parameters toe_sec and t_sec should be referenced to the same,
///       at the same time-scale
///
/// @see IS-GPS-200H, User Algorithm for Ephemeris Determination
int
NavDataFrame::gps_ecef(double toe_sec, double t_sec, double* state, double* Ek_ptr)
const noexcept
{
  int status = 0;
  constexpr double LIMIT  {1e-14};       //  Limit for solving (iteratively) 
                                         //+ the Kepler equation for the 
                                         //+ eccentricity anomaly
  double A  (data__[10]*data__[10]);     //  Semi-major axis
  double n0 (std::sqrt(mi_gps/(A*A*A))); //  Computed mean motion (rad/sec)
  double tk (t_sec-toe_sec);
#ifdef DEBUG
  if (tk<-302400e0 || tk>302400e0) {
    std::cerr<<"\n[ERROR] NavDataFrame::gps_ecef Delta-seconds are off! WTF?";
    return -1;
  }
  if (tk> 302400e0) tk -= 604800e0;
  if (tk<-302400e0) tk += 604800e0;
#endif
  double n  (n0+data__[5]);              //  Corrected mean motion
  double Mk (data__[6]+n*tk);            //  Mean anomaly

  // Solve (iteratively) Kepler's equation for Ek
  double E  (Mk);
  double Ek (0e0);
  double e  (data__[8]);
  int i;
  for (i=0; std::abs(E-Ek)>LIMIT && i<1001; i++) {
    Ek = E;
    E = std::sin(Ek)*e+Mk;
  }
  if (i>=1000) return 1;
  Ek = E;

  if (Ek_ptr) *Ek_ptr = Ek;

  double sinE    (std::sin(E));
  double cosE    (std::cos(E));
  double ecosEm1 (1e0-e*cosE);
  double vk_ar   ((std::sqrt(1e0-e*e)*sinE)/ecosEm1);
  double vk_pr   ((cosE-e)/ecosEm1);
  double vk      (std::atan2(vk_ar, vk_pr));            // True Anomaly
  double cosVk   (std::cos(vk));
  Ek           = std::acos((e+cosVk)/(1e0+e*cosVk));    // Eccentric Anomaly

  // Second Harmonic Perturbations
  double Fk      (vk+data__[17]);                       // Argument of Latitude
  double sin2F   (std::sin(2e0*Fk));
  double cos2F   (std::cos(2e0*Fk));
  double duk     (data__[9]*sin2F  + data__[7]*cos2F);  // Argument of Latitude
                                                        //+ Correction
  double drk     (data__[4]*sin2F  + data__[16]*cos2F); // Radius Correction
  double dik     (data__[14]*sin2F + data__[12]*cos2F); // Inclination Correction

  double uk      (Fk + duk);                            // Corrected Argument 
                                                        //+ of Latitude
  double rk      (A*(1e0-e*std::cos(Ek))+drk);          // Corrected Radius
  double ik      (data__[15]+dik+data__[19]*tk);        // Corrected Inclination
                                
  // Positions in orbital plane
  double xk_dot  (rk*std::cos(uk));
  double yk_dot  (rk*std::sin(uk));
  
  // Corrected longitude of ascending node
  double omega_k (data__[13]+(data__[18]-OMEGAE_dot)*tk-OMEGAE_dot*data__[11]);
  double sinOk   (std::sin(omega_k));
  double cosOk   (std::cos(omega_k));
  double cosik   (std::cos(ik));
  
  state[0] = xk_dot*cosOk - yk_dot*sinOk*cosik;
  state[1] = xk_dot*sinOk + yk_dot*cosOk*cosik;
  state[2] = yk_dot*std::sin(ik);

  // all done
  return status;
}

/// @brief Compute SV Clock Correction
///
/// Determine the effective SV PRN code phase offset referenced to the phase 
/// center of the antennas (∆tsv) with respect to GPS system time (t) at the 
/// time of data transmission. This estimated correction accounts for the 
/// deterministic SV clock error characteristics of bias, drift and aging, as 
/// well as for the SV implementation characteristics of group delay bias and 
/// mean differential group delay. Since these coefficients do not include 
/// corrections for relativistic effects, the user's equipment must determine 
/// the requisite relativistic correction.
/// The user shall correct the time received from the SV with the equation 
/// (in seconds):
/// t = t_sv - Δt_sv
///
/// @param[in]  t     The difference t-t_oc in seconds
/// @param[out] dt_sv SV Clock Correction in seconds; satellite clock bias 
///                   includes relativity correction without code bias (tgd or 
///                   bgd)
/// @param[in]  Ein   If provided, the value to use for Eccentric Anomaly (to
///                   compute the relativistic error term). If not provided,
///                   then Kepler's equation will be used to compute it. If a
///                   user has already computed Ek (e.g. when computing SV
///                   coordinates), then this value could be used here with
///                   reduced accuracy
/// @return Anything other than 0 denotes an error
int
NavDataFrame::gps_dtsv(double dt, double& dt_sv, double* Ein)
const noexcept
{
  constexpr double LIMIT  {1e-14};       //  Limit for solving (iteratively) 
                                         //+ the Kepler equation for the 
                                         //+ eccentricity anomaly
  if (dt> 302400e0) dt -= 604800e0;
  if (dt<-302400e0) dt += 604800e0;

  double Ek (0e0);
  if (!Ein) {
    // Solve (iteratively) Kepler's equation for Ek
    double A  (data__[10]*data__[10]);     //  Semi-major axis
    double n0 (std::sqrt(mi_gps/(A*A*A))); //  Computed mean motion (rad/sec)
    double n  (n0+data__[5]);              //  Corrected mean motion
    double Mk (data__[6]+n*dt);            //  Mean anomaly
    double E  (Mk);
    double e  (data__[8]);
    int i;
    for (i=0; std::abs(E-Ek)>LIMIT && i<1001; i++) {
      Ek = E;
      E = std::sin(Ek)*e+Mk;
    }
    if (i>=1000) return 1;
    Ek = E;
  } else {
    Ek = *Ein;
  }

  // Compute Δtr relativistic correction term (seconds)
  const double Dtr = F_CLOCK * (data__[8]*data__[10]*std::sin(Ek));

  // Compute correction
  double Dtsv = data__[0] + data__[1]*dt + (data__[2]*dt)*dt;
  Dtsv += Dtr;
  dt_sv = Dtsv;

  return 0;
}
