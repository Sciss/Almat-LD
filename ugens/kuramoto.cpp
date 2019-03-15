#include "SC_PlugIn.h"

// number of inputs before array of control signals
#define INPUTOFFSET 2

static InterfaceTable *ft;

struct Kuramoto : public Unit {
  double* phases_prev;
  int n_oscs;
};


struct Hopf : public Unit {
  double x, y, omega;
};


extern "C" {
  void load(InterfaceTable *inTable);

  void Kuramoto_Ctor(Kuramoto* unit);
  void Kuramoto_next_a(Kuramoto *unit, int inNumSamples);
  void Kuramoto_Dtor(Kuramoto *unit);

  void Hopf_Ctor(Hopf* unit);
  void Hopf_next_a(Hopf *unit, int inNumSamples);
  void Hopf_Dtor(Hopf *unit);
  
};


void Kuramoto_Ctor(Kuramoto* unit) {
  SETCALC(Kuramoto_next_a);

  int n = IN0(0);
  float** inputs = unit->mInBuf;  
  unit->n_oscs = n;

  // phases alloc and init
  unit->phases_prev = (double*)RTAlloc(unit->mWorld, n * sizeof(double));
  for (int i = 0; i<n; i++) {
    unit->phases_prev[i] = inputs[INPUTOFFSET+i][0];
  }
  
  Kuramoto_next_a(unit, 1);
}



void update_phases(int i, int start_k, int end_k, int n, double* phases, float** inputs, int this_mode) {

  int kinc = end_k > start_k ? 1 : -1;
  int k = start_k;

  while(k != end_k) {

    double diff = 0.0;
    double this_phase = phases[k];
    float increment = inputs[INPUTOFFSET+n+k][i];
    float phase_input = inputs[INPUTOFFSET+(n*2)+k][i];
    float coupling_internal = inputs[INPUTOFFSET+(n*3)+k][i];
    float coupling_external = inputs[INPUTOFFSET+(n*4)+k][i];
    
    switch (this_mode) {
	
      case 0:
      	// all differences
      	for (int j=0; j < n; ++j) {
      	  diff += sin(phases[j] - this_phase);
      	}
	diff = diff * coupling_internal;
      	diff += sin(phase_input - this_phase) * coupling_external;
      	diff = diff / (double) n;
      	break;
	
      case 1:
      	// differences from neighbors
      	diff += sin(phases[(k-1)%n] - this_phase)  * coupling_internal;
      	diff += sin(phases[(k+1)%n] - this_phase) * coupling_internal;
      	diff += sin(phase_input - this_phase) * coupling_external;
      	diff = diff / 2.f;
      	break;

      case 2:
      	// negative and positive difference
      	diff -= sin(phases[(k-1)%n] - this_phase)  * coupling_internal;
      	diff += sin(phases[(k+1)%n] - this_phase)  * coupling_internal;
      	diff += sin(phase_input - this_phase) * coupling_external;
      	diff = diff / 2.f;
      	break;

    }

    phases[k] = fmod(this_phase + ((increment + diff) * 0.5), twopi);

    k += kinc;
  }
  
}


void Kuramoto_next_a(Kuramoto *unit, int inNumSamples) {
  
  int n = unit->n_oscs;
  double* phases_prev = unit->phases_prev;
  
  // inputs contains: n (init), mode (audio),
  // [n*initPhases (init)], [n*increments (audio)],
  // [n*phaseInput (audio)],  [n*coupling_strengths_internal (audio)],
  // [n*coupling_strengths_external (audio)]
  
  float* mode = IN(1); 
  float** inputs = unit->mInBuf;

  float* out = OUT(0);
  
  for (int i=0; i < inNumSamples; ++i) {
    int this_mode = sc_clip(mode[i], 0.f, 2.f);
      

    // forward
    update_phases(i, 0, n - 1, n, phases_prev, inputs, this_mode);
    // backward
    update_phases(i, n - 1, 0, n, phases_prev, inputs, this_mode);

    for (int k = 0; k < n; ++k) {
      (unit->mOutBuf[k])[i] = phases_prev[k];
    }
  }


}

void Kuramoto_Dtor(Kuramoto *unit)
{

  RTFree(unit->mWorld, unit->phases_prev);
  
}


inline float sqSum(float a, float b) {
  return pow(a,2) + pow(b,2);
}


void Hopf_Ctor(Hopf* unit) {
  SETCALC(Hopf_next_a);

  unit->x = 1.0;
  unit->y = 0.0;
  unit->omega = 0.1;
  
  Hopf_next_a(unit, 1);
}


void Hopf_next_a(Hopf *unit, int inNumSamples) {

  float* force = IN(0);
  float* coupling = IN(1);
  float radius = 0.001;
  float dd = 0.0;
  float x = unit->x;
  float y = unit->y;
  float omega = unit->omega;
  float* out = OUT(0);
  
  for (int i=0; i < inNumSamples; ++i) {

    dd = (coupling[i] * -1.f) * force[i] * y / sqrt(sqSum(x,y));
    omega += dd * 0.5;

    dd = (omega * -1.f * y) + ((radius - sqSum(x,y)) * x) + (coupling[i] * force[i]);
    x += (dd * 0.5);

    dd = (omega * x) + ((radius - sqSum(x,y)) * y);
    y += (dd * 0.5);

    dd = (omega * x) + ((radius - sqSum(x,y)) * y);
    y += (dd * 0.5);


    dd = (omega * -1.f * y) + ((radius - sqSum(x,y)) * x) + (coupling[i] * force[i]);
    x += (dd * 0.5);

    dd = (coupling[i] * -1.f) * force[i] * y / sqrt(sqSum(x,y));
    omega += dd * 0.5;

    out[i] = omega;
  }

  unit->x = x;
  unit->y = y;
  unit->omega = omega;

}

PluginLoad(Kuramoto)
{
  ft = inTable;

  DefineDtorUnit(Kuramoto);
  DefineSimpleUnit(Hopf);
}


