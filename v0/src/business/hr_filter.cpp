// hr_filter.cpp
#include "hr_filter.h"

// SOS values from design_sos.py
// Each row is one biquad: [b0,b1,b2,a1,a2] with a0=1
static const float SOS[3][5] = {
    /* HP stage 1:  b0,    b1,    b2,    a1,    a2 */ 
    { 0.967694809, -1.935389618,  0.967694809, -1.954001962,  0.954619251 },
    /* HP stage 2:  b0,    b1,    b2,    a1,    a2 */ 
    { 1.         , -2.         ,  1.         , -1.980323859,  0.980949464 },
    /* LP stage 1:  b0,    b1,    b2,    a1,    a2 */ 
    { 0.036574836,  0.073149672,  0.036574836, -1.390895281,  0.537194625 }
};

static BiquadCascadeDF2T<3> bp;

void hr_filter_init() {
    // Order: HP stage with lower Q first, then higher Q, then LP
    for (int i=0;i<3;++i) {
        bp.sec[i].b0 = SOS[i][0];
        bp.sec[i].b1 = SOS[i][1];
        bp.sec[i].b2 = SOS[i][2];
        bp.sec[i].a1 = SOS[i][3];
        bp.sec[i].a2 = SOS[i][4];
        bp.sec[i].reset();
    }
}

void hr_filter_process(const float* in, float* out, std::size_t n) {
    bp.processBuffer(in, out, n);
}