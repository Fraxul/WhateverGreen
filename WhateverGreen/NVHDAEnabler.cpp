#include <Headers/kern_util.hpp>
#include <libkern/libkern.h>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pci/IOPCIBridge.h>
#include <IOKit/IODeviceTreeSupport.h>

// Workaround for systems with BIOSes that default-disable the HD Audio function on their NVIDIA GPUs.
// We match the device with a higher IOProbeScore than the NVIDIA drivers, use our probe routine to
// enable the HD Audio function, trigger a PCI rescan, and then return a probe failure so that the
// real driver can continue to load.
//
// References:
// https://bugs.freedesktop.org/show_bug.cgi?id=75985
// https://devtalk.nvidia.com/default/topic/1024022/linux/gtx-1060-no-audio-over-hdmi-only-hda-intel-detected-azalia/
//

class NVHDAEnabler : public IOService {
OSDeclareDefaultStructors(NVHDAEnabler);
public:
  virtual IOService* probe(IOService* provider, SInt32* score) override;
  virtual bool start(IOService* provider) override;
};

OSDefineMetaClassAndStructors(NVHDAEnabler, IOService);

const uint32_t kHDAEnableReg = 0x488;

const uint32_t kHDAEnableBit = 0x02000000;

IOService* NVHDAEnabler::probe(IOService* provider, SInt32* score) {
  IOPCIDevice* pciDevice = OSDynamicCast(IOPCIDevice, provider);
  if (!pciDevice) {
    SYSLOG("NVHDAEnabler", "probe(): pciDevice is NULL\n");
    return NULL;
  }

  uint32_t hda_enable_dword = pciDevice->configRead32(kHDAEnableReg);
  if (hda_enable_dword & kHDAEnableBit) {
    DBGLOG("NVHDAEnabler", "probe(): HDA enable bit is already set, nothing to do\n");
    return NULL;
  }

  DBGLOG("NVHDAEnabler", "probe(): reg is 0x%x, setting HDA enable bit\n", hda_enable_dword);
  hda_enable_dword |= kHDAEnableBit;
  pciDevice->configWrite32(kHDAEnableReg, hda_enable_dword);

  // Verify with readback
  hda_enable_dword = pciDevice->configRead32(kHDAEnableReg);
  DBGLOG("NVHDAEnabler", "probe(): readback: reg is 0x%x\n", hda_enable_dword);

  // Find the parent IOPCIBridge
  IOPCIDevice* parentBridge = OSDynamicCast(IOPCIDevice, pciDevice->getParentEntry(gIODTPlane));
  if (!parentBridge) {
    DBGLOG("NVHDAEnabler", "probe(): Can't find the parent bridge's IOPCIDevice\n");
    return NULL;
  }

  DBGLOG("NVHDAEnabler", "probe(): Requesting parent bridge rescan\n");

  // Mark this device and the parent bridge as needing scanning, then trigger the rescan.
  pciDevice->kernelRequestProbe(kIOPCIProbeOptionNeedsScan);
  parentBridge->kernelRequestProbe(kIOPCIProbeOptionNeedsScan | kIOPCIProbeOptionDone);

  // This probe must always fail so that the real driver can get a chance to load afterwards.
  return NULL;
}

bool NVHDAEnabler::start(IOService* provider) {
  SYSLOG("NVHDAEnabler", "start(): shouldn't be called!\n");
  return false;
}

