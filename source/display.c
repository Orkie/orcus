#include <gp2xregs.h>
#include <orcus.h>

extern void orcus_delay(int loops);

// NOTE! Using lots of magic numbers here which were just lifted straight from an F100 via JTAG while running official GPH firmware, after trying to figure out the values based on the data sheet took too long. These may be different for an F200, to confirm
void orcus_configure_display(bool isF200) {
  REG16(DISPCSETREG) = 0x5E00;//0x6000; // DISPCLKSRC(FPLL_CLK) | DISPCLKDIV(32) | DISPCLKPOL(0);
  REG16(DPC_CNTL) = PAL(0) | CISCYNC(0) | HDTV(0) | DOT(0) | INTERLACE(0) | SYNCCBCR(0) | ESAVEN(0) | DOF(2); //0x5
  REG16(DPC_CLKCNTL) = 0x10; //CLKSRC(2) | CLK2SEL(0) | CLK1SEL(0) | CLK1POL(0);

  REG16(DPC_X_MAX) = 320 - 1;
  REG16(DPC_Y_MAX) = 240 - 1;

  REG16(DPC_HS_WIDTH) = 0x041D; //HSPADCFG(0) | HSWID(30 - 1);
  REG16(DPC_HS_END) = 0x0009; //(20/2)-1;
  REG16(DPC_HS_STR) = 0x0009; //(20/2) + (20%1) - 1;
  REG16(DPC_DE) = 0x250; //DESTR(38-1);
  REG16(DPC_V_SYNC) = 0x0403;
  REG16(DPC_V_END) = 0x0816;
  REG16(DPC_FPIPOL1) = 0x0010;
  REG16(DPC_FPIPOL2) = 0xFFFF;
  REG16(DPC_FPIPOL3) = 0x00FF;

  REG16(DPC_FPIATV1) = 0xFFFF;
  REG16(DPC_FPIATV2) = 0xFFFF;
  REG16(DPC_FPIATV3) = 0xFFFF;

  REG16(DPC_CNTL) |= ENB(1);

  if(isF200) {
    REG16(GPIOFOUT) |= 0xC;

    orcus_delay(20);
    REG16(GPIOBOUT) |= LCD_RESET;
    orcus_delay(50);
    REG16(GPIOBOUT) &= ~LCD_RESET;
    orcus_delay(50);
    REG16(GPIOBOUT) |= LCD_RESET;

    REG16(GPIOLOUT) |= (1 << 11); // backlight on
  }
}

static RgbFormat rgbFormat; 

// page 344 of datasheet for MLC information
void rgbSetPixelFormat(RgbFormat format) {
  rgbFormat = format;
  REG16(MLC_STL_CNTL) &= ~MLC_STL_BPP(3);
  REG16(MLC_STL_CNTL) |= MLC_STL_BPP(format);

  // have to set the scale registers here since they are dependend on pixel format
  rgbSetScale(320, 240);
}

void rgbToggleRegion(RgbRegion region, bool onOff) {
  int shift = (region-1) * 2;
  REG16(MLC_STL_CNTL) = (REG16(MLC_STL_CNTL) & ~(1 << shift)) | 0xAA | (onOff << shift);
  int shift2 = (region-1) + 2;
  REG16(MLC_OVLAY_CNTR) = (REG16(MLC_OVLAY_CNTR) & ~ (1 << shift2)) | (onOff << shift2);
}


static void orcus_rgb_blend(RgbRegion region, BlendingMode mode) {
  int shift = (region-1) * 2;
  REG16(MLC_STL_MIXMUX) = (REG16(MLC_STL_MIXMUX) & ~(3 << shift)) | (mode << shift);
}

static void orcus_rgb_set_alpha(RgbRegion region, uint4_t alpha) {
  int shift = region <= 3 ? (region * 4) : ((region - 3) * 4);
  uint8_t masked_alpha = alpha & 0xF;

  if(region <= 3)
    REG16(MLC_STL_ALPHAL) = (REG16(MLC_STL_ALPHAL) & ~(0xF << shift)) | (masked_alpha << shift);
  else
    REG16(MLC_STL_ALPHAH) = (REG16(MLC_STL_ALPHAH) & ~(0xF << shift)) | (masked_alpha << shift);
}

// alpha is 4 bit - 0-15
void rgbRegionBlendAlpha(RgbRegion region, uint4_t alpha) {
  orcus_rgb_blend(region, ALPHA);
  orcus_rgb_set_alpha(region, alpha);
}


void rgbRegionBlendColourKey(RgbRegion region) {
  orcus_rgb_set_alpha(region, 15);
  orcus_rgb_blend(region, COLOUR_KEY);
  
}

void rgbRegionNoBlend(RgbRegion region) {
  orcus_rgb_blend(region, NO_BLENDING);
}

void rgbColourKey(uint8_t r, uint8_t g, uint8_t b) {
  REG16(MLC_STL_CKEY_GR) = g << 8 | r;
  REG16(MLC_STL_CKEY_B) = b;
}

// setting srcW or srcH to 0 disables scaling for that axis
void rgbSetScale(int srcW, int srcH) {
  uint16_t horizontalPixelWidth =
    rgbFormat == P4BPP ? srcW / 2
  : rgbFormat == P8BPP ? srcW
  : rgbFormat == RGB565 ? srcW * 2
  : srcW * 3; // RGB888
  
  uint16_t hScale = srcW == 0 ? 0 : (srcW*1024)/320;
  uint32_t vScale = srcH == 0 ? 0 : (srcH*horizontalPixelWidth)/240;

  REG16(MLC_STL_HSC) = hScale & 0xFFFF;
  REG16(MLC_STL_VSCL) = vScale & 0xFFFF;
  REG16(MLC_STL_VSCH) = vScale >> 16;  

  REG16(MLC_STL_HW) = horizontalPixelWidth;
}

void rgbSetFbAddress(void* fb) {
  uint32_t addr = (uint32_t) fb;
  REG16(MLC_STL_OADRL) = addr & 0xFFFF;
  REG16(MLC_STL_OADRH) = addr >> 16;
  REG16(MLC_STL_EADRL) = addr & 0xFFFF;
  REG16(MLC_STL_EADRH) = addr >> 16;
}

// region 5 cannot be moved
void rgbSetRegionPosition(RgbRegion region, int x, int y, int width, int height) {
  if(region < 5) {
    uint16_t baseAddr = MLC_STLn_STX + ((region - 1) * 8);
    REG16(baseAddr) = x;
    REG16(baseAddr+2) = x + width - 1;
    REG16(baseAddr+4) = y;
    REG16(baseAddr+6) = y + height - 1;
  }
}

void rgbSetPalette(uint32_t* colours, int count, uint8_t startIdx) {
  REG16(MLC_STL_PALLT_A) = ((uint16_t)startIdx)*2;
  for(int i = count ; i-- ; ) {
    REG16(MLC_STL_PALLT_D) = (colours[i] >> 8) & 0xFFFF; // write G8B8 first
    REG16(MLC_STL_PALLT_D) = (colours[i] >> 24) & 0xFFFF; // write R8 second
  }
}

bool lcdVSync() {
  return REG16(GPIOBPINLVL) & BIT(4) ? true : false;
}

void lcdWaitNextVSync() {
  while(lcdVSync());
  while(!lcdVSync());
}

bool lcdHSync() {
  return REG16(GPIOBPINLVL) & BIT(5) ? true : false;
}

void lcdWaitNextHSync() {
  while(lcdHSync());
  while(!lcdHSync());
}
