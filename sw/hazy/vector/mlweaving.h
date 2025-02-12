// Copyright 2018 Zeke Wang, ETH, Zurich
// Author : Zeke Wang (zeke.wang [at] inf.ethz.ch) 
//Licensed under the Apache License, Version 2.0 (the "License");
//you may not use this file except in compliance with the License.
//You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
//Unless required by applicable law or agreed to in writing, software
//distributed under the License is distributed on an "AS IS" BASIS,
//WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//See the License for the specific language governing permissions and
//limitations under the License.

#ifndef MLWEAVING_H
#define MLWEAVING_H


// This file is mainly about how to compress the dataset into MLWeaving layout and 
// to get data (char or short) out of MLWeaving layout, 
// MLWeaving: pack 8 samples together, 

#include "string.h"
#include "hazy/vector/fvector.h"
#include <immintrin.h>


namespace hazy {
namespace vector {

#define BITS_OF_CL      512
#define NUM_BANKS       8
#define BITS_OF_BANK    (BITS_OF_CL/NUM_BANKS)

/*
//Suppose the size of each value of training dataset is 32-bit, always true for our case...
uint32_t compute_num_CLs_per_sample(uint32_t dr_numFeatures) 
{
  //With the chunk of 512 features...
  uint32_t main_num 		  = (dr_numFeatures/BITS_OF_CL)*32; //It is for CLs
  uint32_t rem_num			  = 0;

  //For the remainder of dr_numFeatures...
  uint32_t remainder_features = dr_numFeatures & (BITS_OF_CL - 1); 
  if (remainder_features == 0)
	rem_num = 0;
  else if (remainder_features <= 64)
	rem_num = 4;
  else if (remainder_features <= 128) 
	rem_num = 8;
  else if (remainder_features <= 256) 
	rem_num = 16;
  else	
	rem_num = 32;
  //printf("main_num = %d, rem_num = %d\t", main_num, rem_num);
  return main_num + rem_num;
}
*/


//This function performs weaving on the input data array: src.
//Input : src  (dense, unsigned int) 	
//Output: dest (in MLWeaving)
void mlweaving_on_sample(uint32_t *dest, uint32_t *src, uint32_t numSamples, uint32_t numFeatures) 
{
	uint32_t address_index         = 0;
	///Do the bitWeaving to the training data...
	for (uint32_t i = 0; i < numSamples; i+=NUM_BANKS)
	{   
		uint32_t samples_in_batch = ( (i+NUM_BANKS)<numSamples )? NUM_BANKS:(numSamples-i); 
		// j; //Deal with the main part of numFeatures.
		for (uint32_t j = 0; j < numFeatures; j += BITS_OF_BANK)//(numFeatures/BITS_OF_BANK)*BITS_OF_BANK
		{
			uint32_t bits_in_batch = ( (j+BITS_OF_BANK)<numFeatures )? BITS_OF_BANK:(numFeatures-j); 
			uint32_t tmp_buffer[512] = {0};
			//1: initilization off tmp buffer..
			for (int k = 0; k < samples_in_batch; k++)//NUM_BANKS
				for (int m = 0; m < bits_in_batch; m++) //BITS_OF_BANK
					tmp_buffer[ k*BITS_OF_BANK+m ] = src[ (i + k)*numFeatures + (j+m) ];

			//2: focus on the data from index: j...
			for (int k = 0; k < 32; k++)
			{	
				uint32_t result_buffer[16] = {0};
				//2.1: re-order the data according to the bit-level...
				for (int m = 0; m < 512; m++)
				{
					result_buffer[m>>5] = result_buffer[m>>5] | ((tmp_buffer[m] >>31)<<(m&31));
					tmp_buffer[m]       = tmp_buffer[m] << 1;				
				}
			    //2.2: store the bit-level result back to the memory...
				dest[address_index++] = result_buffer[0];
				dest[address_index++] = result_buffer[1];
				dest[address_index++] = result_buffer[2];
				dest[address_index++] = result_buffer[3];
				dest[address_index++] = result_buffer[4];
				dest[address_index++] = result_buffer[5];
				dest[address_index++] = result_buffer[6];
				dest[address_index++] = result_buffer[7];
				dest[address_index++] = result_buffer[8];
				dest[address_index++] = result_buffer[9];
				dest[address_index++] = result_buffer[10];
				dest[address_index++] = result_buffer[11];
				dest[address_index++] = result_buffer[12];
				dest[address_index++] = result_buffer[13];
				dest[address_index++] = result_buffer[14];
				dest[address_index++] = result_buffer[15];
			}
		}
	}
}

//This function retrives the sample from the mlweaving layout with address: src. 
//dest: destination fvector 
//src : address of mlweaving array
//index: sample index
//num_bits: number of bits to retrieve. 
//T: template is used to generalize to uchar, ushort, uint.
template <typename T>
void inline retrieve_from_mlweaving(FVector<T> & dest, uint32_t *src, uint32_t index, uint32_t num_bits) 
{	
	//dest's information
	uint32_t numFeatures        = dest.size;
	T*       vec_char           = dest.values;

	//aligned number of features. 
	uint32_t numFeaturesAlign   = ( (numFeatures+(BITS_OF_BANK-1))&(~(BITS_OF_BANK-1)) ); //round up to the nearest mulitple of BITS_OF_BANK

	//calculate the address of sample of the index: index. 
	uint32_t  chunk_offset      = (index/NUM_BANKS) * numFeaturesAlign*NUM_BANKS; //chunk index * chunk size
	uint32_t  sample_offset     = (index%NUM_BANKS) * (BITS_OF_BANK/32); //chunk index * chunk size
	uint32_t* sample_addr       = src + chunk_offset + sample_offset; //identify the address of the sampel (index)

	uint32_t t_bits_min_1       = sizeof(T)*8 - 1;

	for (size_t i = 0; i < numFeatures; i++) 
	{  //prepare one number for each iteration.  
		uint32_t main_offset = ( i/BITS_OF_BANK	  ) * BITS_OF_CL; //main index * size of chunk
		uint32_t int_offset  = ( i&(BITS_OF_BANK-1) )/32;
		uint32_t bit_offset  = i & 31;

		//The next 32 CLs contains the information of the feature. 
		T result = 0;
		uint32_t tmp;
		for (uint32_t j = 0; j < num_bits; j++)
		{
							     //main		      bit	          which ints 
		  tmp	  = sample_addr[main_offset + (BITS_OF_CL/32)*j + int_offset]; //16=512/32
		  result |= ( ( (tmp&(1<<bit_offset)) >> bit_offset ) << (t_bits_min_1-j) ); //
		}
		vec_char[i] = result; 
	}
}



} // namespace vector
} // namespace hazy
#endif
