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

#include "oran-ru-energy-model.h"

#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/pointer.h"
#include "ns3/energy-source.h"
#include "ns3/basic-energy-source.h"
#include "ns3/lte-module.h"       // for LteEnbPhy type & GetTxPower()
#include <cmath>
#include <limits>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("OranRuEnergyModel");

/* ---------------- OranRuPowerModel ---------------- */

NS_OBJECT_ENSURE_REGISTERED (OranRuPowerModel);

TypeId
OranRuPowerModel::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::OranRuPowerModel")
    .SetParent<Object> ()
    .SetGroupName ("Energy")
    .AddConstructor<OranRuPowerModel> ()
    .AddAttribute ("EtaPA",
                   "Power amplifier efficiency (0..1). Typical 0.25..0.40",
                   DoubleValue (0.30),
                   MakeDoubleAccessor (&OranRuPowerModel::m_etaPa),
                   MakeDoubleChecker<double> (0.0, 1.0))
    .AddAttribute ("FixedOverheadW",
                   "Per-TRX fixed overhead (RF+BB+misc) [W] (P0).",
                   DoubleValue (80.0),
                   MakeDoubleAccessor (&OranRuPowerModel::m_fixedOverheadW),
                   MakeDoubleChecker<double> (0.0))
    .AddAttribute ("MmwaveOverheadW",
                   "Per-TRX mmWave-specific overhead [W] (0 for sub-6 GHz).",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&OranRuPowerModel::m_mmwaveOverheadW),
                   MakeDoubleChecker<double> (0.0))
    .AddAttribute ("DeltaAf",
                   "Antenna feeder loss (fraction of power lost). Example: 0.5 ~ 3 dB.",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&OranRuPowerModel::m_deltaAf),
                   MakeDoubleChecker<double> (0.0, 0.99))
    .AddAttribute ("DeltaDC",
                   "DC-DC conversion loss (fraction).",
                   DoubleValue (0.07),
                   MakeDoubleAccessor (&OranRuPowerModel::m_deltaDc),
                   MakeDoubleChecker<double> (0.0, 0.99))
    .AddAttribute ("DeltaMS",
                   "Mains supply loss (fraction).",
                   DoubleValue (0.09),
                   MakeDoubleAccessor (&OranRuPowerModel::m_deltaMs),
                   MakeDoubleChecker<double> (0.0, 0.99))
    .AddAttribute ("DeltaCool",
                   "Cooling loss (fraction). Set to 0 for small cells.",
                   DoubleValue (0.10),
                   MakeDoubleAccessor (&OranRuPowerModel::m_deltaCool),
                   MakeDoubleChecker<double> (0.0, 0.99))
    .AddAttribute ("NumTrx",
                   "Number of TRX chains.",
                   UintegerValue (1u),
                   MakeUintegerAccessor (&OranRuPowerModel::m_nTrx),
                   MakeUintegerChecker<uint32_t> (1u))
    .AddAttribute ("Vdc",
                   "DC supply voltage [V].",
                   DoubleValue (48.0),
                   MakeDoubleAccessor (&OranRuPowerModel::m_vdc),
                   MakeDoubleChecker<double> (1.0))
    .AddAttribute ("SleepPowerW",
                   "Per-TRX sleep/standby power [W].",
                   DoubleValue (5.0),
                   MakeDoubleAccessor (&OranRuPowerModel::m_psleepW),
                   MakeDoubleChecker<double> (0.0))
    .AddAttribute ("SleepThresholdDbm",
                   "At/below this TxPower (dBm), treat RU as sleeping.",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&OranRuPowerModel::m_sleepThresholdDbm),
                   MakeDoubleChecker<double> (-200.0, 200.0))
    .AddAttribute ("LossesInSleep",
                   "Apply supply/cooling losses to sleep power.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&OranRuPowerModel::m_lossesInSleep),
                   MakeBooleanChecker ());
  return tid;
}

OranRuPowerModel::OranRuPowerModel ()
  : m_etaPa (0.30),
    m_fixedOverheadW (80.0),
    m_mmwaveOverheadW (0.0),
    m_deltaAf (0.0),
    m_deltaDc (0.07),
    m_deltaMs (0.09),
    m_deltaCool (0.10),
    m_nTrx (1u),
    m_vdc (48.0),
    m_psleepW (5.0),
    m_sleepThresholdDbm (0.0),
    m_lossesInSleep (false)
{
}

double
OranRuPowerModel::DbmToWatt (double dbm) const
{
  return std::pow (10.0, (dbm - 30.0) / 10.0);
}

double
OranRuPowerModel::GetPowerW (double txPowerDbm) const
{
  // Sleep / standby
  if (txPowerDbm <= m_sleepThresholdDbm)
    {
      double p = static_cast<double> (m_nTrx) * m_psleepW;
      if (m_lossesInSleep)
        {
          const double eff = (1.0 - m_deltaDc) * (1.0 - m_deltaMs) * (1.0 - m_deltaCool);
          if (eff > 0.0)
            {
              p /= eff;
            }
        }
      return p;
    }

  // Active mode
  const double pTxW = DbmToWatt (txPowerDbm);
  const double denomPa = m_etaPa * (1.0 - m_deltaAf);
  const double pPa = (denomPa > 0.0) ? (pTxW / denomPa) : std::numeric_limits<double>::infinity ();
  const double perTrx = pPa + m_fixedOverheadW + m_mmwaveOverheadW;
  const double lossesEff = (1.0 - m_deltaDc) * (1.0 - m_deltaMs) * (1.0 - m_deltaCool);
  const double totalNoLoss = static_cast<double> (m_nTrx) * perTrx;
  const double pTotal = (lossesEff > 0.0) ? (totalNoLoss / lossesEff) : std::numeric_limits<double>::infinity ();
  return pTotal;
}

double
OranRuPowerModel::GetCurrentA (double txPowerDbm) const
{
  const double pW = GetPowerW (txPowerDbm);
  return pW / m_vdc;
}

/* ---------------- OranRuDeviceEnergyModel ---------------- */

NS_OBJECT_ENSURE_REGISTERED (OranRuDeviceEnergyModel);

TypeId
OranRuDeviceEnergyModel::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::OranRuDeviceEnergyModel")
    .SetParent<DeviceEnergyModel> ()
    .SetGroupName ("Energy")
    .AddConstructor<OranRuDeviceEnergyModel> ()
    .AddAttribute ("TxPowerDbm",
                   "Fallback TxPower (dBm) when no LteEnbPhy is attached.",
                   DoubleValue (30.0),
                   MakeDoubleAccessor (&OranRuDeviceEnergyModel::m_txPowerDbm),
                   MakeDoubleChecker<double> (-200.0, 200.0))
    .AddAttribute ("PowerModel",
                   "Pointer to the OranRuPowerModel used for current computation.",
                   PointerValue (CreateObject<OranRuPowerModel> ()),
                   MakePointerAccessor (&OranRuDeviceEnergyModel::m_model),
                   MakePointerChecker<OranRuPowerModel> ())
    .AddAttribute ("LteEnbPhy",
                   "Optional pointer to the eNB PHY; if set, current is computed from its TxPower.",
                   PointerValue (nullptr),
                   MakePointerAccessor (&OranRuDeviceEnergyModel::m_enbPhy),
                   MakePointerChecker<LteEnbPhy> ())
    .AddTraceSource ("CurrentA",
                     "Reported device current (A).",
                     MakeTraceSourceAccessor (&OranRuDeviceEnergyModel::m_traceCurrentA),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("PowerW",
                     "Reported device power (W).",
                     MakeTraceSourceAccessor (&OranRuDeviceEnergyModel::m_tracePowerW),
                     "ns3::TracedValueCallback::Double")
    .AddTraceSource ("TxPowerDbmTrace",
                     "TxPower (dBm) used for computation (from PHY or attribute).",
                     MakeTraceSourceAccessor (&OranRuDeviceEnergyModel::m_traceTxPowerDbm),
                     "ns3::TracedValueCallback::Double");
  return tid;
}

OranRuDeviceEnergyModel::OranRuDeviceEnergyModel ()
  : m_enbPhy (nullptr),
    m_model (CreateObject<OranRuPowerModel> ()),
    m_source (nullptr),
    m_txPowerDbm (30.0),
    m_initialized (false),
    m_lastUpdate (Seconds (0.0)),
    m_lastCurrentA (0.0),
    m_accumulatedEnergyJ (0.0)
{
  NS_LOG_FUNCTION (this);
}

OranRuDeviceEnergyModel::~OranRuDeviceEnergyModel ()
{
  NS_LOG_FUNCTION (this);
  m_enbPhy = nullptr;
  m_model = nullptr;
  m_source = nullptr;
}

void
OranRuDeviceEnergyModel::SetEnergySource (Ptr<EnergySource> source)
{
  NS_LOG_FUNCTION (this << source);
  // Do NOT call base; in some ns-3.41 builds the base has no definition.
  m_source = source;
}

void
OranRuDeviceEnergyModel::SetLteEnbPhy (Ptr<LteEnbPhy> phy)
{
  m_enbPhy = phy;
}

Ptr<LteEnbPhy>
OranRuDeviceEnergyModel::GetLteEnbPhy () const
{
  return m_enbPhy;
}

void
OranRuDeviceEnergyModel::SetRuPowerModel (Ptr<OranRuPowerModel> m)
{
  m_model = m;
}

Ptr<OranRuPowerModel>
OranRuDeviceEnergyModel::GetRuPowerModel () const
{
  return m_model;
}

double
OranRuDeviceEnergyModel::ReadTxPowerDbm () const
{
  if (m_enbPhy != nullptr)
    {
      // Reads the current TxPower attribute from the PHY (dBm)
      return m_enbPhy->GetTxPower ();
    }
  return m_txPowerDbm;
}

double
OranRuDeviceEnergyModel::DoGetCurrentA (void) const
{
  NS_ASSERT_MSG (m_model != nullptr, "OranRuDeviceEnergyModel requires a valid OranRuPowerModel");

  const double txDbm = ReadTxPowerDbm ();
  const double currentA = m_model->GetCurrentA (txDbm );
  const double powerW = m_model->GetPowerW (txDbm );

  // On-demand energy integration using last current and elapsed time
  const Time now = Simulator::Now ();
  if (m_initialized)
    {
      const double dt = (now - m_lastUpdate).GetSeconds ();
      double v = 48.0;
      Ptr<BasicEnergySource> b = DynamicCast<BasicEnergySource> (m_source);
      if (b)
        {
          v = b->GetSupplyVoltage ();
        }
      m_accumulatedEnergyJ += m_lastCurrentA * v * dt;
    }
  else
    {
      m_initialized = true;
    }

  m_lastUpdate = now;
  m_lastCurrentA = currentA;

  // Update traces for logging / validation
  m_traceTxPowerDbm = txDbm;
  m_traceCurrentA = currentA;
  m_tracePowerW = powerW;

  return currentA;
}

double
OranRuDeviceEnergyModel::GetTotalEnergyConsumption () const
{
  // Flush integration up to "now" using last current
  const Time now = Simulator::Now ();
  if (m_initialized)
    {
      double v = 48.0;
      Ptr<BasicEnergySource> b = DynamicCast<BasicEnergySource> (m_source);
      if (b)
        {
          v = b->GetSupplyVoltage ();
        }
      const double dt = (now - m_lastUpdate).GetSeconds ();
      return m_accumulatedEnergyJ + (m_lastCurrentA * v * dt);
    }
  return m_accumulatedEnergyJ;
}

void
OranRuDeviceEnergyModel::ChangeState (int /*newState*/)
{
  // This model does not maintain discrete radio states; current derives from PHY Tx power.
}

void
OranRuDeviceEnergyModel::HandleEnergyDepletion ()
{
  NS_LOG_INFO ("OranRuDeviceEnergyModel: Energy depleted");
}

void
OranRuDeviceEnergyModel::HandleEnergyRecharged ()
{
  NS_LOG_INFO ("OranRuDeviceEnergyModel: Energy recharged");
}

void
OranRuDeviceEnergyModel::HandleEnergyChanged ()
{
  // No special action; current is recomputed on demand and energy integrated in DoGetCurrentA().
}

} // namespace ns3
