#include "papito.h"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <vector>

using namespace std;

vector<string> papiEvents;
int eventset=PAPI_NULL;

void input()
{
  ifstream infile("counters.in");
  string line;
  while (getline(infile, line))
  {
      istringstream iss(line);
      string a;
      if (!(iss >> a)) { break; } // error
      papiEvents.push_back(a);
  }
}

void papito_init()
{
	input();

	int retval;

	/* Initialize the library */
	// https://linux.die.net/man/3/papi_library_init
	retval = PAPI_library_init(PAPI_VER_CURRENT);
  if (retval != PAPI_VER_CURRENT)
	{
      printf("PAPI library init error!\n");
      exit(1);
  }
}

void handle_error (int retval)
{
	if(retval == 1) printf("Erro ao criar eventSet ");
	if(retval == 2) printf("Erro ao add events ao eventSet ");
	if(retval == 3) printf("Erro ao iniciar ");
	if(retval == 4) printf("Erro stop ");
	if(retval == 5) printf("Erro reset ");
	if(retval == 30) printf("Erro PAPI_overflow");
	if(retval == 31) printf("Erro PAPI_state");

    printf("PAPI error %d: %s\n", retval, PAPI_strerror(retval));
    exit(1);
}

void papito_start()
{
	int i;
  eventset=PAPI_NULL;

  int codeEvents[papiEvents.size()];
  for (int i = 0; i < papiEvents.size(); i++)
  {
    if (papiEvents[i] == "PAPI_L1_DCM") codeEvents[i] = PAPI_L1_DCM;
    else if (papiEvents[i] == "PAPI_L1_ICM") codeEvents[i] = PAPI_L1_ICM;
    else if (papiEvents[i] == "PAPI_L2_DCM") codeEvents[i] = PAPI_L2_DCM;
    else if (papiEvents[i] == "PAPI_L2_ICM") codeEvents[i] = PAPI_L2_ICM;
    else if (papiEvents[i] == "PAPI_L1_TCM") codeEvents[i] = PAPI_L1_TCM;
    else if (papiEvents[i] == "PAPI_L2_TCM") codeEvents[i] = PAPI_L2_TCM;
    else if (papiEvents[i] == "PAPI_L3_TCM") codeEvents[i] = PAPI_L3_TCM;
    else if (papiEvents[i] == "PAPI_CA_SNP") codeEvents[i] = PAPI_CA_SNP;
    else if (papiEvents[i] == "PAPI_CA_SHR") codeEvents[i] = PAPI_CA_SHR;
    else if (papiEvents[i] == "PAPI_CA_CLN") codeEvents[i] = PAPI_CA_CLN;
    else if (papiEvents[i] == "PAPI_CA_ITV") codeEvents[i] = PAPI_CA_ITV;
    else if (papiEvents[i] == "PAPI_L3_LDM") codeEvents[i] = PAPI_L3_LDM;
    else if (papiEvents[i] == "PAPI_TLB_DM") codeEvents[i] = PAPI_TLB_DM;
    else if (papiEvents[i] == "PAPI_TLB_IM") codeEvents[i] = PAPI_TLB_IM;
    else if (papiEvents[i] == "PAPI_L1_LDM") codeEvents[i] = PAPI_L1_LDM;
    else if (papiEvents[i] == "PAPI_L1_STM") codeEvents[i] = PAPI_L1_STM;
    else if (papiEvents[i] == "PAPI_L2_LDM") codeEvents[i] = PAPI_L2_LDM;
    else if (papiEvents[i] == "PAPI_L2_STM") codeEvents[i] = PAPI_L2_STM;
    else if (papiEvents[i] == "PAPI_PRF_DM") codeEvents[i] = PAPI_PRF_DM;
    else if (papiEvents[i] == "PAPI_MEM_WCY") codeEvents[i] = PAPI_MEM_WCY;
    else if (papiEvents[i] == "PAPI_STL_ICY") codeEvents[i] = PAPI_STL_ICY;
    else if (papiEvents[i] == "PAPI_FUL_ICY") codeEvents[i] = PAPI_FUL_ICY;
    else if (papiEvents[i] == "PAPI_STL_CCY") codeEvents[i] = PAPI_STL_CCY;
    else if (papiEvents[i] == "PAPI_FUL_CCY") codeEvents[i] = PAPI_FUL_CCY;
    else if (papiEvents[i] == "PAPI_BR_UCN") codeEvents[i] = PAPI_BR_UCN;
    else if (papiEvents[i] == "PAPI_BR_CN") codeEvents[i] = PAPI_BR_CN;
    else if (papiEvents[i] == "PAPI_BR_TKN") codeEvents[i] = PAPI_BR_TKN;
    else if (papiEvents[i] == "PAPI_BR_NTK") codeEvents[i] = PAPI_BR_NTK;
    else if (papiEvents[i] == "PAPI_BR_MSP") codeEvents[i] = PAPI_BR_MSP;
    else if (papiEvents[i] == "PAPI_BR_PRC") codeEvents[i] = PAPI_BR_PRC;
    else if (papiEvents[i] == "PAPI_TOT_INS") codeEvents[i] = PAPI_TOT_INS;
    else if (papiEvents[i] == "PAPI_LD_INS") codeEvents[i] = PAPI_LD_INS;
    else if (papiEvents[i] == "PAPI_SR_INS") codeEvents[i] = PAPI_SR_INS;
    else if (papiEvents[i] == "PAPI_FP_INS") codeEvents[i] = PAPI_FP_INS;
    else if (papiEvents[i] == "PAPI_BR_INS") codeEvents[i] = PAPI_BR_INS;
    else if (papiEvents[i] == "PAPI_RES_STL") codeEvents[i] = PAPI_RES_STL;
    else if (papiEvents[i] == "PAPI_TOT_CYC") codeEvents[i] = PAPI_TOT_CYC;
    else if (papiEvents[i] == "PAPI_LST_INS") codeEvents[i] = PAPI_LST_INS;
    else if (papiEvents[i] == "PAPI_L2_DCA") codeEvents[i] = PAPI_L2_DCA;
    else if (papiEvents[i] == "PAPI_L3_DCA") codeEvents[i] = PAPI_L3_DCA;
    else if (papiEvents[i] == "PAPI_L2_DCR") codeEvents[i] = PAPI_L2_DCR;
    else if (papiEvents[i] == "PAPI_L3_DCR") codeEvents[i] = PAPI_L3_DCR;
    else if (papiEvents[i] == "PAPI_L2_DCW") codeEvents[i] = PAPI_L2_DCW;
    else if (papiEvents[i] == "PAPI_L3_DCW") codeEvents[i] = PAPI_L3_DCW;
    else if (papiEvents[i] == "PAPI_L2_ICH") codeEvents[i] = PAPI_L2_ICH;
    else if (papiEvents[i] == "PAPI_L2_ICA") codeEvents[i] = PAPI_L2_ICA;
    else if (papiEvents[i] == "PAPI_L3_ICA") codeEvents[i] = PAPI_L3_ICA;
    else if (papiEvents[i] == "PAPI_L2_ICR") codeEvents[i] = PAPI_L2_ICR;
    else if (papiEvents[i] == "PAPI_L3_ICR") codeEvents[i] = PAPI_L3_ICR;
    else if (papiEvents[i] == "PAPI_L2_TCA") codeEvents[i] = PAPI_L2_TCA;
    else if (papiEvents[i] == "PAPI_L3_TCA") codeEvents[i] = PAPI_L3_TCA;
    else if (papiEvents[i] == "PAPI_L2_TCR") codeEvents[i] = PAPI_L2_TCR;
    else if (papiEvents[i] == "PAPI_L3_TCR") codeEvents[i] = PAPI_L3_TCR;
    else if (papiEvents[i] == "PAPI_L2_TCW") codeEvents[i] = PAPI_L2_TCW;
    else if (papiEvents[i] == "PAPI_L3_TCW") codeEvents[i] = PAPI_L3_TCW;
    else if (papiEvents[i] == "PAPI_SP_OPS") codeEvents[i] = PAPI_SP_OPS;
    else if (papiEvents[i] == "PAPI_DP_OPS") codeEvents[i] = PAPI_DP_OPS;
    else if (papiEvents[i] == "PAPI_VEC_SP") codeEvents[i] = PAPI_VEC_SP;
    else if (papiEvents[i] == "PAPI_VEC_DP") codeEvents[i] = PAPI_VEC_DP;
    else if (papiEvents[i] == "PAPI_REF_CYC") codeEvents[i] = PAPI_REF_CYC;
  }

	if (PAPI_create_eventset(&eventset) != PAPI_OK) handle_error(1);
	// Add Total Instructions Executed to our EventSet
	if (PAPI_add_events(eventset, codeEvents, papiEvents.size()) != PAPI_OK) handle_error(2);
	// Start counting events in the Event Set
	if (PAPI_start(eventset) != PAPI_OK) handle_error(3);

}

void papito_end()
{
	int t;
	long_long values[papiEvents.size()]; // Cada thread registra seu contadores aqui.

	// Parar a contagem dos eventos
		if (PAPI_stop(eventset, values) != PAPI_OK) handle_error(4);
		if (PAPI_cleanup_eventset(eventset) != PAPI_OK) handle_error(20);
		if (PAPI_destroy_eventset(&eventset) != PAPI_OK) handle_error(21);

  for (int i = 0; i < papiEvents.size(); i++)
  {
    cout << string(papiEvents[i]).substr(5, string(papiEvents[i]).size()) << "\t";
  }
  cout << endl;

	// Totalizando os eventos
  for (int i = 0; i < papiEvents.size(); i++)
  {
    cout << values[i] << "\t";
  }
  cout << endl<< endl;

}
