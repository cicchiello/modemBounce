#ifndef VBAT_H
#define VBAT_H

#define VBAT_SAMPLES_PER_READING (16)

class VBat {
 public:
  static VBat *singleton(); // there can be only one

  float readVolts() const;
  
 private:
  VBat();
};

#endif
