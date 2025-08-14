/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Oran RU Energy Model for ns-3 / ns3-oran
 *
 * A lightweight, reusable Radio Unit (RU) power model and a DeviceEnergyModel
 * that plugs into ns-3's energy framework. It computes current draw from the
 * LTE eNB PHY TxPower (if provided) or from a user-set TxPowerDbm attribute.
 *
 * References:
 * - EARTH model (Auer et al., 2011)
 * - O-RAN / RU-centric modeling as described in the accompanying paper.
 *
 * Author: Abdul Wadud, UCD, Ireland
 */

#ifndef ORAN_RU_ENERGY_MODEL_H
#define ORAN_RU_ENERGY_MODEL_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/traced-value.h"
#include "ns3/nstime.h"
#include "ns3/type-id.h"
#include "ns3/device-energy-model.h"

namespace ns3 {

class EnergySource;
class LteEnbPhy;

/**
 * \brief Parametric RU power model (Object) that converts TxPower (dBm) to
 *        RU power (W) and current (A) using EARTH-style losses and RU-specific
 *        constants. Can be reused by any DeviceEnergyModel or app code.
 */
class OranRuPowerModel : public Object
{
public:
  static TypeId GetTypeId ();
  OranRuPowerModel ();
  ~OranRuPowerModel () override = default;

  /** \brief Compute RU total power (W) from Tx power (dBm). */
  double GetPowerW (double txPowerDbm) const;

  /** \brief Compute RU current (A) from Tx power (dBm). */
  double GetCurrentA (double txPowerDbm) const;

  /** Helpers exposed for testing / papers */
  double DbmToWatt (double dbm) const;

private:
  // RU / Hardware parameters (ns-3 Attributes)
  double   m_etaPa;             //!< Power amplifier efficiency [0..1]
  double   m_fixedOverheadW;    //!< P0 (RF+BB+misc) per TRX [W]
  double   m_mmwaveOverheadW;   //!< Optional mmWave overhead per TRX [W]
  double   m_deltaAf;           //!< Antenna feeder fractional power loss (e.g., 0.5 ~ 3 dB)
  double   m_deltaDc;           //!< DC-DC loss fraction
  double   m_deltaMs;           //!< Mains supply loss fraction
  double   m_deltaCool;         //!< Cooling loss fraction (macro only)
  uint32_t m_nTrx;              //!< Number of TRX chains
  double   m_vdc;               //!< DC supply voltage [V]
  double   m_psleepW;           //!< Per-TRX sleep/standby power [W]
  double   m_sleepThresholdDbm; //!< TxPower dBm at/below which RU is in sleep
  bool     m_lossesInSleep;     //!< Whether to apply losses in sleep mode
};

/**
 * \brief A DeviceEnergyModel that uses OranRuPowerModel to report current
 *        consumption to an EnergySource. If an LteEnbPhy is attached, it reads
 *        TxPower live; otherwise it uses the TxPowerDbm attribute.
 *
 * Implements ns-3.41 DeviceEnergyModel pure virtuals.
 */
class OranRuDeviceEnergyModel : public DeviceEnergyModel
{
public:
  static TypeId GetTypeId ();
  OranRuDeviceEnergyModel ();
  ~OranRuDeviceEnergyModel () override;

  // --- DeviceEnergyModel API --- //
  void SetEnergySource (Ptr<EnergySource> source) override;
  // Use base DeviceEnergyModel::GetEnergySource()
  double DoGetCurrentA (void) const override;

  // Required pure virtuals (provide minimal, functional implementations)
  double GetTotalEnergyConsumption () const override;
  void ChangeState (int newState) override;
  void HandleEnergyDepletion () override;
  void HandleEnergyRecharged () override;
  void HandleEnergyChanged () override;

  // --- Convenience setters --- //
  void SetLteEnbPhy (Ptr<LteEnbPhy> phy);
  Ptr<LteEnbPhy> GetLteEnbPhy () const;

  void SetRuPowerModel (Ptr<OranRuPowerModel> m);
  Ptr<OranRuPowerModel> GetRuPowerModel () const;

private:
  // Utility to fetch current TxPower dBm (from PHY or attribute fallback).
  double ReadTxPowerDbm () const;

  // State
  Ptr<LteEnbPhy>        m_enbPhy;     //!< Optional, if attached
  Ptr<OranRuPowerModel> m_model;      //!< Power model
  Ptr<EnergySource>   m_source;     //!< Bound energy source (cached)

  // Attribute fallback when no PHY is attached
  double m_txPowerDbm;                //!< Fallback Tx power [dBm]

  // Energy accounting (simple on-demand integration)
  mutable bool m_initialized;
  mutable Time m_lastUpdate;
  mutable double m_lastCurrentA;
  mutable double m_accumulatedEnergyJ;

  // Traces (helpful for logging / validation)
  mutable TracedValue<double> m_traceCurrentA;
  mutable TracedValue<double> m_tracePowerW;
  mutable TracedValue<double> m_traceTxPowerDbm;
};

} // namespace ns3

#endif /* ORAN_RU_ENERGY_MODEL_H */
