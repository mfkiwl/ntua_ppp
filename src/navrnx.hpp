#ifndef __NAVIGATION_RINEX_HPP__
#define __NAVIGATION_RINEX_HPP__

#include <fstream>
#include "ggdatetime/dtcalendar.hpp"
#include "satsys.hpp"
#ifdef DEBUG
#include "ggdatetime/datetime_write.hpp"
#endif

namespace ngpt
{

/// QZSS:       data__[0]  : Time of Clock
///             data__[0]  : SV clock bias in seconds
///             data__[1]  : SV clock drift in m/sec
///             data__[2]  : SV clock drift rate in m/sec^2
///             data__[x]  : IODE Issue of Data, Ephemeris
///             data__[x]  : Crs(meters)
///             data__[x]  : Delta n (radians/sec)
///             data__[x]  : M0 (radians)
/// BeiDou:     data__[0]  : Time of Clock in BDT time
///             data__[0]  : SV clock bias in seconds
///             data__[1]  : SV clock drift in m/sec
///             data__[2]  : SV clock drift rate in m/sec^2
///             data__[x]  : AODE Age of Data, Ephemeris (as specified in 
///                          BeiDou ICD Table Section 5.2.4.11 Table 5-8) and 
///                          field range is: 0-31
///             data__[x]  : Crs (meters)
///             data__[x]  : Delta n (radians/sec)
///             data__[x]  : M0 (radians) 
/// SBAS:       data__[0]  : Time of Clock in GPS time 
///             data__[0]  : SV clock bias in sec, aka aGf0
///             data__[1]  : SV relative frequency bias, aka aGf1
///             data__[2]  : Transmission time of message (start of the 
///                          message) in GPS seconds of the week
///             data__[x]  : Satellite position X (km)
///             data__[x]  : velocity X dot (km/sec)
///             data__[x]  : X acceleration (km/sec2)
///             data__[x]  : Health:SBAS:See Section 8.3.3 bit mask for:
///                          health, health availability and User Range Accuracy. 
/// IRNSS:      data__[0]  : Time of Clock in IRNSS time
///             data__[0]  : SV clock bias in seconds
///             data__[1]  : SV clock drift in m/sec
///             data__[2]  : SV clock drift rate in m/sec^2
///             data__[x]  : IODEC Issue of Data, Ephemeris and Clock
///             data__[x]  : Crs(meters)
///             data__[x]  : Deltan (radians/sec)
///             data__[x]  : M0(radians)
class NavDataFrame
{
public:

  /// @brief Null constructor
  NavDataFrame() noexcept
  {};

  /// @brief Set from a RINEX 3.x navigation data block
  int
  set_from_rnx3(std::ifstream& inp) noexcept;
  
  /// @brief get SV coordinates (WGS84) from navigation block
  /// see IS-GPS-200H, User Algorithm for Ephemeris Determination
  int
  gps_ecef(double toe_sec, double t_sec, double* state, double* Ek=nullptr)
  const noexcept;

  template<typename T>
    ngpt::datetime<T>
    gps__toe2date() const noexcept
  {
    ngpt::gps_week wk (static_cast<int>(data__[21]));
    ngpt::seconds  sc (static_cast<long>(data__[11]));
    return ngpt::datetime<T>(wk, sc);
  }
  
  ngpt::datetime<seconds>
  glo_tb2date(bool to_MT) const noexcept;

  int
  glo_ecef(double t_insod, double tb_sod, double* state)
  const noexcept;
  int
  glo_ecef2(double t_insod, double tb_sod, double& x, double& y, double& z, double* vel=nullptr)
  const noexcept;
  
  int
  gps_dtsv(double dt, double& dt_sv, double* Ek_in=nullptr) const noexcept;
  int
  glo_dtsv(double t_tm, double toe_tm, double& dtsv) const noexcept;
  
  template<typename T>
    int
    gps_stateNclock(ngpt::datetime<T> t, double* state, double& dt)
    const noexcept
  {
    int status = 0;
    // reference time for SV position computation, is ToE
    datetime<T> toe (this->gps__toe2date<T>());
    double toe_sec = toe.sec().to_fractional_seconds();
    double t_sec   = t.sec().to_fractional_seconds();
    // reference t to ToE
    if (t.mjd()>toe.mjd()) {
      t_sec += 86400e0;
    } else if (t.mjd()<toe.mjd()) {
      t_sec = t_sec - 86400e0;
    }
    if ( (status=gps_ecef(toe_sec, t_sec, state)) ) return status;
    // dt from ToC
    auto   dt_ = ngpt::delta_sec(t, toc__);
    double dti = dt_.to_fractional_seconds();
    if ( (status=gps_dtsv(dti, dt)) ) return status;
    return status;
  }
  
  /// @brief Compute SV centre of mass state vector in ECEF PZ90 frame at
  ///        epoch epoch using the simplified algorithm
  /// @param[in] epoch The time in UTC for which we want the SV state
  /// @param[out] The SV centre of mass state vector in meters, meters/sec
  template<typename T>
    int
    glo_stateNclock(ngpt::datetime<T> t, double* state, double& dt)
    const noexcept
  {
    int status = 0;
    constexpr seconds secmt (10800L);
    // t_i and t_b to MT
    t.add_seconds(secmt);
    ngpt::datetime<seconds> tb = glo_tb2date(true);
    double sec = t.sec().to_fractional_seconds();
    double tb_sec = tb.sec().to_fractional_seconds();
    // reference ti and tb to the same day (it may happen? that ti and tb are
    // in different days)
    if (t.mjd()>tb.mjd()) {
      sec += 86400e0;
    } else if (t.mjd()<tb.mjd()) {
      sec = sec - 86400e0;
    }
    if ( (status=glo_ecef(sec, tb_sec, state)) ) return status;
    if ( (status=glo_dtsv(sec, tb_sec, dt)) ) return status;
    return 0;
  }
  
  template<typename T>
    int
    gps_dtsv(const ngpt::datetime<T>& epoch, double& dtsv)
    const noexcept
  {
    T dsec = ngpt::delta_sec<T, ngpt::seconds>(epoch, toc__);
    double sec = static_cast<double>(dsec.as_underlying_type());
    return this->gps_dtsv(sec, dtsv);
  }

  double
  data(int idx) const noexcept { return data__[idx]; }

  double&
  data(int idx) noexcept { return data__[idx]; }

  SATELLITE_SYSTEM
  sys() const noexcept { return sys__; }

  int
  prn() const noexcept { return prn__; }

  ngpt::datetime<ngpt::seconds>
  toc() const noexcept { return toc__; }

  void
  set_toc(ngpt::datetime<ngpt::seconds> d) noexcept {toc__=d;}

  SATELLITE_SYSTEM
  satsys() const noexcept {return sys__;}
  
private:
  SATELLITE_SYSTEM              sys__{};     ///< Satellite system
  int                           prn__{};     ///< PRN as in Rinex 3x
  ngpt::datetime<ngpt::seconds> toc__{};     ///< Time of clock
  double                        data__[31]{};///< Data block
};

class NavigationRnx
{
public:
  /// Let's not write this more than once.
  typedef std::ifstream::pos_type pos_type;
  
  /// @brief Constructor from filename
  explicit
  NavigationRnx(const char*);
  
  /// @brief Destructor (closing the file is not mandatory, but nevertheless)
  ~NavigationRnx() noexcept 
  { 
    if (__istream.is_open()) __istream.close();
  }
  
  /// @brief Copy not allowed !
  NavigationRnx(const NavigationRnx&) = delete;

  /// @brief Assignment not allowed !
  NavigationRnx& operator=(const NavigationRnx&) = delete;
  
  /// @brief Move Constructor.
  NavigationRnx(NavigationRnx&& a)
  noexcept(std::is_nothrow_move_constructible<std::ifstream>::value) = default;

  /// @brief Move assignment operator.
  NavigationRnx& operator=(NavigationRnx&& a) 
  noexcept(std::is_nothrow_move_assignable<std::ifstream>::value) = default;

  /// @brief Read, resolved and store next navigation data block
  int
  read_next_record(NavDataFrame&) noexcept;
  
  /// @brief Check the first line of the following message to get the sat. sys
  ngpt::SATELLITE_SYSTEM
  peak_satsys(int&) noexcept;

  /// @brief Read and skip the next navigation message
  int
  ignore_next_block() noexcept;

  /// @brief Set the stream to end of header
  void
  rewind() noexcept;

private:
  
  /// @brief Read RINEX header; assign info
  int
  read_header() noexcept;

  std::string            __filename;    ///< The name of the file
  std::ifstream          __istream;     ///< The infput (file) stream
  SATELLITE_SYSTEM       __satsys;      ///< satellite system
  float                  __version;     ///< Rinex version (e.g. 3.4)
  pos_type               __end_of_head; ///< Mark the 'END OF HEADER' field
};// NavigationRnx

}// ngpt

#endif
