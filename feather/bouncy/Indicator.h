#pragma once

#include <stdint.h>

class IndicatorLED {
 public:
  IndicatorLED();

  void off();

  void setRed(bool v);
  void setGreen(bool v);
  void setBlue(bool v);

  void toggleRed() {setRed(!getRed());}
  void toggleGreen() {setGreen(!getGreen());}
  void toggleBlue() {setBlue(!getBlue());}
  void toggleAll() {toggleRed(); toggleGreen(); toggleBlue();}
  
  bool getRed() const {return mR;}
  bool getGreen() const {return mG;}
  bool getBlue() const {return mB;}
  
 private:
  bool mR, mG, mB;
};
