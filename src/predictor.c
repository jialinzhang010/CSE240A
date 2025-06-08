//========================================================//
//  predictor.c                                           //
//  Source file for the Branch Predictor                  //
//                                                        //
//  Implement the various branch predictors below as      //
//  described in the README                               //
//========================================================//
#include <stdio.h>
#include "predictor.h"

//
// TODO:Student Information
//
const char *studentName = "Jialin Zhang";
const char *studentID   = "A69033045";
const char *email       = "jiz282@ucsd.edu";

//------------------------------------//
//      Predictor Configuration       //
//------------------------------------//

// Handy Global for use in output routines
const char *bpName[4] = { "Static", "Gshare",
                          "Tournament", "Custom" };

int ghistoryBits; // Number of bits used for Global History
int lhistoryBits; // Number of bits used for Local History
int pcIndexBits;  // Number of bits used for PC index
int bpType;       // Branch Prediction Type
int verbose;

//------------------------------------//
//      Predictor Data Structures     //
//------------------------------------//


//
//TODO: Add your own Branch Predictor data structures here
//

// Gshare
uint32_t ghr;
uint8_t *gshare_bht;

// Tournament
uint32_t *local_history_table;
uint8_t *local_bht;
uint8_t *global_bht;
uint8_t *choice_table;

//------------------------------------//
//        Predictor Functions         //
//------------------------------------//

// Initialize the predictor
//
void init_predictor()
{
  switch (bpType) {
    case GSHARE:
      ghr = 0;
      gshare_bht = (uint8_t *)malloc(sizeof(uint8_t) * (1 << ghistoryBits));
      for (int i = 0; i < (1 << ghistoryBits); i++)
        gshare_bht[i] = WN;
      break;

    case TOURNAMENT:
      ghr = 0;
      local_history_table = (uint32_t *)malloc(sizeof(uint32_t) * (1 << pcIndexBits));
      local_bht = (uint8_t *)malloc(sizeof(uint8_t) * (1 << lhistoryBits));
      global_bht = (uint8_t *)malloc(sizeof(uint8_t) * (1 << ghistoryBits));
      choice_table = (uint8_t *)malloc(sizeof(uint8_t) * (1 << ghistoryBits));

      for (int i = 0; i < (1 << pcIndexBits); i++) local_history_table[i] = 0;
      for (int i = 0; i < (1 << lhistoryBits); i++) local_bht[i] = WN;
      for (int i = 0; i < (1 << ghistoryBits); i++) {
        global_bht[i] = WN;
        choice_table[i] = WT;
      }
      break;

    case CUSTOM:
      break;
  }
}


// Make a prediction for conditional branch instruction at PC 'pc'
// Returning TAKEN indicates a prediction of taken; returning NOTTAKEN
// indicates a prediction of not taken
//
uint8_t
make_prediction(uint32_t pc)
{
  //
  //TODO: Implement prediction scheme
  //

  // Make a prediction based on the bpType
  switch (bpType) {
    case STATIC:
      return TAKEN;
    case GSHARE: {
      uint32_t index = (pc ^ ghr) & ((1 << ghistoryBits) - 1);
      return gshare_bht[index] >= WT ? TAKEN : NOTTAKEN;
    }
    case TOURNAMENT: {
      uint32_t global_index = ghr & ((1 << ghistoryBits) - 1);
      uint32_t local_index = pc & ((1 << pcIndexBits) - 1);
      uint32_t local_history = local_history_table[local_index];
      uint32_t local_bht_index = local_history & ((1 << lhistoryBits) - 1);

      uint8_t local_pred = local_bht[local_bht_index] >= WT ? TAKEN : NOTTAKEN;
      uint8_t global_pred = global_bht[global_index] >= WT ? TAKEN : NOTTAKEN;

      return choice_table[global_index] >= WT ? global_pred : local_pred;
    }
    case CUSTOM:
    default:
      break;
  }

  // If there is not a compatable bpType then return NOTTAKEN
  return NOTTAKEN;
}

// Train the predictor the last executed branch at PC 'pc' and with
// outcome 'outcome' (true indicates that the branch was taken, false
// indicates that the branch was not taken)
//
void train_predictor(uint32_t pc, uint8_t outcome)
{
  switch (bpType) {
    case GSHARE: {
      uint32_t index = (pc ^ ghr) & ((1 << ghistoryBits) - 1);
      if (outcome == TAKEN) {
        if (gshare_bht[index] < ST) gshare_bht[index]++;
      } else {
        if (gshare_bht[index] > SN) gshare_bht[index]--;
      }
      ghr = ((ghr << 1) | outcome) & ((1 << ghistoryBits) - 1);
      break;
    }

    case TOURNAMENT: {
      uint32_t global_index = ghr & ((1 << ghistoryBits) - 1);
      uint32_t local_index = pc & ((1 << pcIndexBits) - 1);
      uint32_t local_history = local_history_table[local_index];
      uint32_t local_bht_index = local_history & ((1 << lhistoryBits) - 1);

      uint8_t local_pred = local_bht[local_bht_index] >= WT ? TAKEN : NOTTAKEN;
      uint8_t global_pred = global_bht[global_index] >= WT ? TAKEN : NOTTAKEN;

      if (outcome == TAKEN) {
        if (local_bht[local_bht_index] < ST) local_bht[local_bht_index]++;
      } else {
        if (local_bht[local_bht_index] > SN) local_bht[local_bht_index]--;
      }
      if (outcome == TAKEN) {
        if (global_bht[global_index] < ST) global_bht[global_index]++;
      } else {
        if (global_bht[global_index] > SN) global_bht[global_index]--;
      }
      if (local_pred != global_pred) {
        if (global_pred == outcome)
          choice_table[global_index] = (choice_table[global_index] < ST) ? choice_table[global_index] + 1 : ST;
        else
          choice_table[global_index] = (choice_table[global_index] > SN) ? choice_table[global_index] - 1 : SN;
      }
      local_history_table[local_index] = ((local_history << 1) | outcome) & ((1 << lhistoryBits) - 1);
      ghr = ((ghr << 1) | outcome) & ((1 << ghistoryBits) - 1);
      break;
    }

    case CUSTOM:
      break;
  }
}

