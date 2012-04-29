/*
// This file is a part of Dao standard modules.
// Copyright (C) 2006-2012, Limin Fu. Email: daokoder@gmail.com
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy of this 
// software and associated documentation files (the "Software"), to deal in the Software 
// without restriction, including without limitation the rights to use, copy, modify, merge, 
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons 
// to whom the Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or 
// substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#ifndef __DAO_JIT_H__
#define __DAO_JIT_H__

#include<vector>
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Instructions.h"
#include "llvm/Support/IRBuilder.h"


extern "C"{
#include"daoArray.h"
#include"daoVmcode.h"
#include"daoValue.h"
#include"daoRoutine.h"
#include"daoProcess.h"
#include"daoVmspace.h"
#include"daoGC.h"

void DaoJIT_Init( DaoVmSpace *vms, DaoJIT *jit );
void DaoJIT_Quit();

void DaoJIT_Free( void *jitdata );
void DaoJIT_Compile( DaoRoutine *routine, DaoOptimizer *optimizer );
void DaoJIT_Execute( DaoProcess *process, DaoJitCallData *data, int jitcode );

}

using namespace llvm;

typedef int (*DaoJitFunction)( DaoJitCallData *data );

struct DaoJitFunctionData
{
	Function       *llvmFunction;
	DaoJitFunction  funcPointer;

	DaoJitFunctionData( Function *function = NULL, DaoJitFunction fpter = NULL ){
		llvmFunction = function;
		funcPointer = fpter;
	}
};

struct DaoJitHandle : public IRBuilder<>
{
	DaoRoutine   *routine;
	DaoOptimizer *optimizer;
	DaoCnode  *currentNode;
	DaoCnode  *firstNode;
	DaoCnode  *lastNode;

	int start;
	int end;

	Function   *jitFunction;
	BasicBlock *entryBlock;
	BasicBlock *secondBlock; // the block after the entry;
	BasicBlock *lastBlock;

	Value *estatus; // exception status
	Value *localValues;  // jitcdata->localConsts : DaoValue*[]*
	Value *localConsts;  // jitcdata->localValues : DaoValue*[]*
	Value *objectValues; // jitcdata->objectValues: DaoValue*[]*
	Value *classValues;  // jitcdata->classValues : DaoValue*[]*
	Value *classConsts;  // jitcdata->classConsts : DaoValue*[]*
	Value *globalValues; // jitcdata->globalValues: DaoValue*[]*
	Value *globalConsts; // jitcdata->globalConsts: DaoValue*[]*
	Value *processes;  // jitcdata->process: DaoProcess*[]*

	// Direct values: single-definition values.
	// Such values do not explicitly use the stack, or explicitly
	// store to and load from memory:
	std::vector<Value*> directValues; // DaoValue**

	// Stack values: multiple-definitions or multiple-uses values.
	// They are allocated on the program stack at the entry block,
	// and may have values loaded from the VM stack.
	// Modification to the value is stored in the stack.
	// These values may be stored back to the VM stack,
	// if they are still alive at the exit of the JIT code.
	std::vector<Value*> stackValues; // DaoValue**

	std::vector<Value*> localRefers; // DaoValue**

	std::vector<std::pair<Value*,BasicBlock*> > dataItems; // DaoValue**

	std::map<int,Value*> mapObjectValueRefers;
	std::map<int,Value*> mapClassValueRefers;
	std::map<int,Value*> mapGlobalValueRefers;
	std::map<int,Value*> mapClassConstValues;
	std::map<int,Value*> mapGlobalConstValues;

	DaoJitHandle( LLVMContext & ctx, DaoRoutine *rout=NULL, DaoOptimizer *opt=NULL ) 
		: IRBuilder<>( ctx )
	{
		routine = rout;
		optimizer = opt;
		currentNode = NULL;
		lastNode = NULL;
	}

	Function* Compile( int start, int end );

	Function* NewFunction( DaoRoutine *routine, int id );
	BasicBlock* NewBlock( int vmc );
	BasicBlock* NewBlock( DaoVmCodeX *vmc );

	void SetValueName( Value *value, const char *name, int id );

	Value* GetLocalConstant( int id );
	Value* GetLocalReference( int reg );
	Value* GetLocalValue( int reg );

	Value* GetObjectValueRefer( int index );
	Value* GetObjectValueValue( int index );
	Value* GetClassValueRefer( int index );
	Value* GetClassValueValue( int index );
	Value* GetGlobalValueRefer( int index );
	Value* GetGlobalValueValue( int index );

	Value* GetUpValueRefer( int up, int index );
	Value* GetUpValueValue( int up, int index );

	Value* GetClassConstValue( int index );
	Value* GetGlobalConstValue( int index );

	Value* GetValueTypePointer( Value *value ); // DaoValue->type
	Value* GetValueDataPointer( Value *value ); // DaoValue->value: for int, float, double;

	// DaoInteger->value, DaoFloat->value, DaoDouble->value:
	Value* GetValueNumberPointer( Value *value, Type *type );
	Value* GetValueNumberValue( Value *value, Type *type );

	Value* Dereference( Value *value );
	Value* CastIntegerPointer( Value *value ); // to dint*
	Value* CastFloatPointer( Value *value ); // to float*
	Value* CastDoublePointer( Value *value ); // to double*
	Value* GetValueItem( Value *array, Value *index );

	Value* CastIntegerValuePointer( Value *value ); // to DaoInteger*
	Value* CastFloatValuePointer( Value *value ); // to DaoFloat*
	Value* CastDoubleValuePointer( Value *value ); // to DaoDouble*

	Value* GetDirectValue( int reg );

	int IsDirectValue( int reg );
	void StoreNumber( Value *value, int reg );

	void ClearTempOperand( int reg );
	void ClearTempOperand( DaoVmCodeX *vmc );
	
	void AddReturnCodeChecking( Value *retcode, int vmc );
	// index: dint; size: size_t* or int*;
	Value* AddIndexChecking( Value *index, Value *size, int vmc );

	Value* GetItemValue( int reg, int field, int *maycache );

	Value* GetNumberOperand( int reg ); // int
	Value* GetTupleItems( int reg ); // DaoValue*[]*
	Value* GetArrayItem( int reg, int index, int vmc ); // daoint/float/double*
	Value* GetArrayItemMI( int reg, int index, int vmc ); // daoint/float/double*
	Value* GetListItem( int reg, int index, int vmc ); // DaoValue*
	Value* GetClassConstant( int reg, int field ); // Value*
	Value* GetClassStatic( int reg, int field ); // Value*
	Value* GetObjectConstant( int reg, int field ); // Value*
	Value* GetObjectStatic( int reg, int field ); // Value*
	Value* GetObjectVariable( int reg, int field ); // Value*

	Value* MoveValue( Value *dA, Value *dC, Type *type );
};

#endif
