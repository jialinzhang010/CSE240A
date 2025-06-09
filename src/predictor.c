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
const char *bpName[5] = { "Static", "Gshare",
                          "Tournament", "Custom", "TAGE" };

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

// Custom
#define GLOBAL_HIST_BITS 13
#define LOCAL_HIST_BITS 8
#define LOCAL_PHT_SIZE (1 << LOCAL_HIST_BITS)
#define GLOBAL_PHT_SIZE (1 << GLOBAL_HIST_BITS)
#define LOCAL_HISTORY_TABLE_SIZE 1024

uint16_t global_history = 0;

uint8_t get_prediction(uint8_t counter) {
    return counter >= WT ? TAKEN : NOTTAKEN;
}

uint8_t update_counter(uint8_t counter, uint8_t outcome) {
    if (outcome == TAKEN) return (counter < ST) ? counter + 1 : ST;
    else return (counter > SN) ? counter - 1 : SN;
}



// Add TAGE data structures after the Custom section
// TAGE Predictor Configuration
#define TAGE_NUM_COMPONENTS 4      // Reduced to 3 tagged tables + base
#define TAGE_BASE_BITS 11          // Reduced to 2K entries
#define TAGE_TAG_WIDTH 8           // Reduced tag width
#define TAGE_USEFUL_BITS 2         // Keep same
#define TAGE_TABLE_BITS 10         // 1K entries per tagged table

// TAGE table entry structure
typedef struct {
    uint8_t ctr;       // 3-bit prediction counter
    uint16_t tag;      // Tag for partial matching
    uint8_t useful;    // 2-bit useful counter
} tage_entry_t;

// TAGE predictor state
typedef struct {
    uint8_t *base_predictor;              // Bimodal base predictor
    tage_entry_t **tables;                // Tagged tables
    int *history_lengths;                 // History lengths for each table
    int *table_sizes;                     // Size of each table
    uint64_t global_history;              // Global history register
    int *table_indices;                   // Current indices for each table
    uint16_t *table_tags;                 // Current tags for each table
    int provider_component;               // Which component provided prediction
    int altpred_component;                // Alternative prediction component
    uint8_t provider_pred;                // Provider's prediction
    uint8_t altpred;                      // Alternative prediction
} tage_predictor_t;

tage_predictor_t tage;


//------------------------------------//
//        Predictor Functions         //
//------------------------------------//

// TAGE helper functions
uint32_t tage_hash(uint32_t pc, uint64_t history, int hist_len) {
    uint64_t result = pc;
    for (int i = 0; i < hist_len; i++) {
        if (history & (1ULL << i)) {
            result ^= (pc >> (i % 16));
        }
    }
    return result;
}

uint16_t tage_compute_tag(uint32_t pc, uint64_t history, int hist_len) {
    uint32_t tag = pc ^ (pc >> hist_len);
    uint64_t hist_compressed = history;
    for (int i = 0; i < hist_len; i += TAGE_TAG_WIDTH) {
        tag ^= (hist_compressed & ((1 << TAGE_TAG_WIDTH) - 1));
        hist_compressed >>= TAGE_TAG_WIDTH;
    }
    return tag & ((1 << TAGE_TAG_WIDTH) - 1);
}

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
      global_bht = (uint8_t *)malloc(sizeof(uint8_t) * GLOBAL_PHT_SIZE);
      choice_table = (uint8_t *)malloc(sizeof(uint8_t) * GLOBAL_PHT_SIZE);
      local_bht = (uint8_t *)malloc(sizeof(uint8_t) * LOCAL_PHT_SIZE);
      local_history_table = (uint32_t *)malloc(sizeof(uint32_t) * LOCAL_HISTORY_TABLE_SIZE);

      for (int i = 0; i < GLOBAL_PHT_SIZE; i++) {
        global_bht[i] = WN;
        choice_table[i] = WT;
      }
      for (int i = 0; i < LOCAL_PHT_SIZE; i++) local_bht[i] = WN;
      for (int i = 0; i < LOCAL_HISTORY_TABLE_SIZE; i++) local_history_table[i] = 0;
      break;

    case TAGE:
    // Initialize history lengths (adjusted for 4 components)
    tage.history_lengths = (int*)malloc(sizeof(int) * TAGE_NUM_COMPONENTS);
    tage.history_lengths[0] = 0;   // Base predictor
    tage.history_lengths[1] = 4;   // Shorter histories
    tage.history_lengths[2] = 16;
    tage.history_lengths[3] = 100;
    
    // Initialize table sizes
    tage.table_sizes = (int*)malloc(sizeof(int) * TAGE_NUM_COMPONENTS);
    tage.table_sizes[0] = 1 << TAGE_BASE_BITS;  // 2K entries for base
    for (int i = 1; i < TAGE_NUM_COMPONENTS; i++) {
      tage.table_sizes[i] = 1 << TAGE_TABLE_BITS;  // 1K entries
    }
    
    // Allocate base predictor
    tage.base_predictor = (uint8_t*)malloc(sizeof(uint8_t) * tage.table_sizes[0]);
    for (int i = 0; i < tage.table_sizes[0]; i++) {
      tage.base_predictor[i] = WN;
    }
    
    // Allocate tagged tables
    tage.tables = (tage_entry_t**)malloc(sizeof(tage_entry_t*) * TAGE_NUM_COMPONENTS);
    for (int i = 1; i < TAGE_NUM_COMPONENTS; i++) {
      tage.tables[i] = (tage_entry_t*)malloc(sizeof(tage_entry_t) * tage.table_sizes[i]);
      for (int j = 0; j < tage.table_sizes[i]; j++) {
        tage.tables[i][j].ctr = WN;
        tage.tables[i][j].tag = 0;
        tage.tables[i][j].useful = 0;
      }
    }
    
    // Initialize other state
    tage.global_history = 0;
    tage.table_indices = (int*)malloc(sizeof(int) * TAGE_NUM_COMPONENTS);
    tage.table_tags = (uint16_t*)malloc(sizeof(uint16_t) * TAGE_NUM_COMPONENTS);
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
    case CUSTOM: {
      uint32_t global_idx = global_history & (GLOBAL_PHT_SIZE - 1);
      uint32_t local_idx = pc & (LOCAL_HISTORY_TABLE_SIZE - 1);
      uint8_t local_hist = local_history_table[local_idx];

      uint8_t local_pred = get_prediction(local_bht[local_hist]);
      uint8_t global_pred = get_prediction(global_bht[global_idx]);

      return (choice_table[global_idx] >= WT) ? global_pred : local_pred;
    }
    case TAGE: {
      // Compute indices and tags for all components
      tage.table_indices[0] = pc & (tage.table_sizes[0] - 1);
      for (int i = 1; i < TAGE_NUM_COMPONENTS; i++) {
        uint32_t index = tage_hash(pc, tage.global_history, tage.history_lengths[i]);
        tage.table_indices[i] = index & (tage.table_sizes[i] - 1);
        tage.table_tags[i] = tage_compute_tag(pc, tage.global_history, tage.history_lengths[i]);
      }
      
      // Find longest matching component
      tage.provider_component = 0;
      tage.altpred_component = 0;
      
      for (int i = TAGE_NUM_COMPONENTS - 1; i > 0; i--) {
        if (tage.tables[i][tage.table_indices[i]].tag == tage.table_tags[i]) {
          tage.provider_component = i;
          break;
        }
      }
      
      // Find alternative prediction
      for (int i = tage.provider_component - 1; i >= 0; i--) {
        if (i == 0 || tage.tables[i][tage.table_indices[i]].tag == tage.table_tags[i]) {
          tage.altpred_component = i;
          break;
        }
      }
      
      // Get predictions
      if (tage.provider_component == 0) {
        tage.provider_pred = tage.base_predictor[tage.table_indices[0]] >= WT ? TAKEN : NOTTAKEN;
      } else {
        tage.provider_pred = tage.tables[tage.provider_component][tage.table_indices[tage.provider_component]].ctr >= 4 ? TAKEN : NOTTAKEN;
      }
      
      if (tage.altpred_component == 0) {
        tage.altpred = tage.base_predictor[tage.table_indices[0]] >= WT ? TAKEN : NOTTAKEN;
      } else {
        tage.altpred = tage.tables[tage.altpred_component][tage.table_indices[tage.altpred_component]].ctr >= 4 ? TAKEN : NOTTAKEN;
      }
      
      // Use provider prediction
      return tage.provider_pred;
    }
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

    case CUSTOM:{
      uint32_t global_idx = global_history & (GLOBAL_PHT_SIZE - 1);
      uint32_t local_idx = pc & (LOCAL_HISTORY_TABLE_SIZE - 1);
      uint8_t local_hist = local_history_table[local_idx];

      uint8_t local_pred = get_prediction(local_bht[local_hist]);
      uint8_t global_pred = get_prediction(global_bht[global_idx]);

      local_bht[local_hist] = update_counter(local_bht[local_hist], outcome);
      global_bht[global_idx] = update_counter(global_bht[global_idx], outcome);

      if (local_pred != global_pred) {
        if (global_pred == outcome && choice_table[global_idx] < ST)
          choice_table[global_idx]++;
        else if (local_pred == outcome && choice_table[global_idx] > SN)
          choice_table[global_idx]--;
      }
      local_history_table[local_idx] = ((local_hist << 1) | outcome) & (LOCAL_PHT_SIZE - 1);
      global_history = ((global_history << 1) | outcome) & (GLOBAL_PHT_SIZE - 1);
      break;
    }

    case TAGE: {
    // Update provider component
    if (tage.provider_component == 0) {
      // Update base predictor
      if (outcome == TAKEN) {
        if (tage.base_predictor[tage.table_indices[0]] < ST)
          tage.base_predictor[tage.table_indices[0]]++;
      } else {
        if (tage.base_predictor[tage.table_indices[0]] > SN)
          tage.base_predictor[tage.table_indices[0]]--;
      }
    } else {
      // Update tagged table entry
      tage_entry_t *entry = &tage.tables[tage.provider_component][tage.table_indices[tage.provider_component]];
      if (outcome == TAKEN) {
        if (entry->ctr < 7) entry->ctr++;
      } else {
        if (entry->ctr > 0) entry->ctr--;
      }
      
      // Update useful counter
      if (tage.provider_pred != tage.altpred) {
        if (tage.provider_pred == outcome && entry->useful < 3) {
          entry->useful++;
        } else if (tage.provider_pred != outcome && entry->useful > 0) {
          entry->useful--;
        }
      }
    }
    
    // Allocate new entries on misprediction
    if (tage.provider_pred != outcome) {
      // Find a table to allocate in
      for (int i = tage.provider_component + 1; i < TAGE_NUM_COMPONENTS; i++) {
        tage_entry_t *entry = &tage.tables[i][tage.table_indices[i]];
        
        // Check if entry is available (useful == 0)
        if (entry->useful == 0) {
          entry->tag = tage.table_tags[i];
          entry->ctr = (outcome == TAKEN) ? 4 : 3;
          entry->useful = 0;
          break;
        }
      }
      
      // Decay useful counters periodically
      if ((tage.global_history & 0xFF) == 0xFF) {
        for (int i = 1; i < TAGE_NUM_COMPONENTS; i++) {
          for (int j = 0; j < tage.table_sizes[i]; j++) {
            if (tage.tables[i][j].useful > 0) {
              tage.tables[i][j].useful--;
            }
          }
        }
      }
    }
    
    // Update global history
    tage.global_history = (tage.global_history << 1) | outcome;
    break;
  }
    default:
      break;
  }

}

