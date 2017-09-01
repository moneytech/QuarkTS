/*******************************************************************************
 *  QuarkTS - A Non-Preemptive Task Scheduler for low-range MCUs
 *  Version : 4.3.6
 *  Copyright (C) 2012 Eng. Juan Camilo Gomez C. MSc. (kmilo17pet@gmail.com)
 *
 *  QuarkTS is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License (LGPL)as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  QuarkTS is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*******************************************************************************/

/*
For documentation, read the wiki
https://github.com/kmilo17pet/QuarkTS/wiki
and the available API
https://github.com/kmilo17pet/QuarkTS/wiki/APIs
*/

#include "QuarkTS.h"
/*=========================== QuarkTS Private Data ===========================*/
static volatile QuarkTSCoreData_t QUARKTS;
static volatile qClock_t _qSysTick_Epochs_ = 0;
static qTask_t *__qCurrExTask = NULL;
/*========================= QuarkTS Private Methods===========================*/
static void _qTriggerEvent(qTask_t *Task, qTrigger_t Event);
static void _qTaskChainbyPriority(void);
static qTask_t* _qPrioQueueExtract(void);
static void _qTriggerIdleTask(void);
static void _qTriggerReleaseSchedEvent(void);
static qSize_t _qRBufferValidPowerOfTwo(qSize_t k);
static qSize_t _qRBufferCount(qRBuffer_t *obj);
static qBool_t _qRBufferFull(qRBuffer_t *obj);
static qTrigger_t _qCheckRBufferEvents(qTask_t *Task);
/*========================== QuarkTS Private Macros ==========================*/
#define _Q_ENTER_CRITICAL()                     if(QUARKTS.I_Disable != NULL) QUARKTS.Flag.IntFlags = QUARKTS.I_Disable()
#define _Q_EXIT_CRITICAL()                      if(QUARKTS.I_Restorer != NULL) QUARKTS.I_Restorer(QUARKTS.Flag.IntFlags)
#define _Q_TASK_DEADLINE_REACHED(_TASK_)        ( ((_qSysTick_Epochs_ - _TASK_->ClockStart)>=_TASK_->Interval) || _TASK_->Interval == TIME_INMEDIATE)
#define _Q_TASK_HAS_PENDING_ITERS(_TASK_)       (_TASK_->Iterations>0 || _TASK_->Iterations==PERIODIC)
#define _Q_MAIN_SCHEDULE(_core_)                qMainSchedule: if(_core_.Flag.ReleaseSched) goto qReleasedSchedule
#define _Q_RESUME_SCHEDULE(_after_release_)     goto qMainSchedule; qReleasedSchedule: _after_release_()
/*============================================================================*/
/*
qTask_t* qTaskSelf(void)

Get current running task handle.

Return value:

    A pointer to the current running task.
    NULL when the scheduler it's in a busy state or when IDLE Task is running.
*/
qTask_t* qTaskSelf(void){
    return __qCurrExTask;
}
/*============================================================================*/
/*
qBool_t qTaskIsEnabled(qTask_t Identifier)

Retrieve the enabled/disabled state

Parameters:

    - Identifier : The task node identifier.

Return value:

    True if the task in on Enabled state, otherwise returns false.
*/    
qBool_t qTaskIsEnabled(qTask_t *Task){
    if(Task==NULL) return qFalse;
    return (qBool_t)Task->Flag.Enabled;
}
/*============================================================================*/
/*void qSchedulerSetIdleTask(qTaskFcn_t Callback)

Establish the IDLE Task Callback

Parameters:

    - IDLE_Callback : A pointer to a void callback method with a qEvent_t 
                      parameter as input argument.
*/
void qSchedulerSetIdleTask(qTaskFcn_t Callback){
    QUARKTS.IDLECallback = Callback;
}
/*============================================================================*/
/*void qSchedulerRelease(void)

Disables the QuarkTS scheduling. The main thread will continue after the
qSchedule() call.
*/
void qSchedulerRelease(void){
    QUARKTS.Flag.ReleaseSched = qTrue;
}
/*============================================================================*/
/*void qSchedulerSetReleaseCallback(qTaskFcn_t Callback)

Set/Change the scheduler release callback function

Parameters:
    - Callback : A pointer to a void callback method with a qEvent_t parameter 
                 as input argument.
*/
void qSchedulerSetReleaseCallback(qTaskFcn_t Callback){
    QUARKTS.ReleaseSchedCallback = Callback;
}
/*============================================================================*/
/*uint32_t qTaskGetCycles(qTask_t Identifier)

Retrieve the number of task activations.

Parameters:

    - Identifier : The task node identifier.

Return value:

    A unsigned long value containing the number of task activations.
*/
uint32_t qTaskGetCycles(qTask_t *Task){
    if (Task==NULL) return 0;
    return Task->Cycles;
}
/*============================================================================*/
/*void qTaskSendEvent(qTask_t *Task, void* eventdata)

Sends a simple asyncrohous event. This method marks the task as ready for 
execution, therefore, the planner will launch the task immediately according to
the execution chain (even if task is disabled) and setting the Trigger flag to
"byAsyncEvent". Specific user-data can be passed through, and will be available
inside the EventData field, only in corresponding launch.

Parameters:

    - Task : Pointer to the task node.

    - eventdata : Specific event user-data.
*/ 
void qTaskSendEvent(qTask_t *Task, void* eventdata){
    if(Task==NULL) return;
    Task->Flag.AsyncRun = 1;
    Task->AsyncData = eventdata;
}
/*============================================================================*/
/*void qTaskSetTime(qTask_t *Task, qTime_t Value)

Set/Change the Task execution interval

Parameters:

    - Task : A pointer to the task node.

    - Value : Execution interval defined in seconds (floating-point format). 
              For inmediate execution (tValue = TIME_INMEDIATE).
*/
void qTaskSetTime(qTask_t *Task, qTime_t Value){
    if(Task==NULL) return;
    Task->Interval = (qClock_t)(Value/QUARKTS.Tick);
}
/*============================================================================*/
/*void qTaskSetIterations(qTask_t *Task, qIteration_t Value)

Set/Change the number of task iterations

Parameters:

    - Task : A pointer to the task node.

    - Value : Number of task executions (Integer value). For indefinite 
              execution (iValue = PERIODIC or INDEFINITE). Tasks do not remember
              the number of iteration set initially. After the iterations are 
              done, internal iteration counter is 0. If you need to perform
              another set of iterations, you need to set the number of 
              iterations again.
*/
void qTaskSetIterations(qTask_t *Task, qIteration_t Value){
    if(Task==NULL) return;
    Task->Iterations = Value;
}
/*============================================================================*/
/*void qTaskSetPriority(qTask_t *Task, qPriority_t Value)

Set/Change the task priority value

Parameters:

    - Task : A pointer to the task node.

    - Value : Priority Value. [0(min) - 255(max)]
*/
void qTaskSetPriority(qTask_t *Task, qPriority_t Value){
    if(Task==NULL) return;
    QUARKTS.Flag.Init = 0; 
    Task->Priority = Value; 
}
/*============================================================================*/
/*void qTaskSetCallback(qTask_t *Task, qTaskFcn_t Callback)

Set/Change the task callback function

Parameters:

    - Task : A pointer to the task node.

    - Callback : A pointer to a void callback method with a qEvent_t parameter 
                 as input argument.
*/
void qTaskSetCallback(qTask_t *Task, qTaskFcn_t CallbackFcn){
    if(Task==NULL) return;
    Task->Callback = CallbackFcn;
}
/*============================================================================*/
/*void qTaskSetState(qTask_t *Task, qState_t State)

Set the task state (Enabled or Disabled)

Parameters:

    - Task : A pointer to the task node.
    - State : qEnabled or qDisabled 
*/
void qTaskSetState(qTask_t *Task, qState_t State){
    if(Task==NULL) return;
    if(State && Task->Flag.Enabled) return;
    Task->Flag.Enabled = State;
    Task->ClockStart = _qSysTick_Epochs_;
}
/*============================================================================*/
/*void qTaskSetData(qTask_t *Task, void* UserData)

Set the task data

Parameters:

    - Task : A pointer to the task node.

Return value:

    A void pointer to the task data.
*/
void qTaskSetData(qTask_t *Task, void* arg){
    if(Task==NULL) return;
    Task->TaskData = arg;
}
/*============================================================================*/
/*void qTaskClearTimeElapsed(qTask_t *Task)

Clear the elapsed time of the task. Restart the internal task tick;

Parameters:

    - Task : A pointer to the task node.
*/
void qTaskClearTimeElapsed(qTask_t *Task){
    if(Task==NULL) return;
    Task->ClockStart = _qSysTick_Epochs_;
}
/*============================================================================*/
/*int qTaskQueueEvent(qTask_t *Task, void* eventdata)

Insert an asyncrohous event in the FIFO priority queue. The task will be ready 
for execution according to the queue extraction (determined by priority), even 
if task is disabled (More info here). When extracted, the scheduler will set 
Trigger flag to "byQueueExtraction". Specific user-data can be passed through, 
and will be available inside the EventData field, only in corresponding launch.

Parameters:

    - Task : A pointer to the task node.

    - eventdata - Specific event user-data.

Return value:

    Return 0 if the event has been inserted in the queue, or -1 if an error 
    occurred (The queue exceeds the size).
*/
int qTaskQueueEvent(qTask_t *Task, void* eventdata){
    if((Task==NULL) || (QUARKTS.QueueIndex>=QUARKTS.QueueSize-1) ) return -1;
    qQueueStack_t qtmp;
    qtmp.Task = Task,
    qtmp.QueueData = eventdata;
    QUARKTS.QueueStack[++QUARKTS.QueueIndex] = qtmp;
    return 0;
}
/*============================================================================*/
/*void qSchedulerSetInterruptsED(void (*Restorer)(void), void (*Disabler)(void))

Set the hardware-specific code for global interrupt enable/disable. 
Setting this allows you to push Interrupt-safe data in the priority queue 
with <qTaskQueueEvent>

Parameters:

    - Restorer : The function with hardware specific code that enables or 
                 restores interrupts.
    - Disabler : The function with hardware specific code that disables interrupts.
*/ 
void qSchedulerSetInterruptsED(void (*Restorer)(uint32_t), uint32_t (*Disabler)(void)){
    QUARKTS.I_Restorer = Restorer;
    QUARKTS.I_Disable = Disabler;
}
/*============================================================================*/
static qTask_t* _qPrioQueueExtract(void){
    qTask_t *Task = NULL;
    uint8_t i;
    uint8_t IndexTaskToExtract = 0;
    if(QUARKTS.QueueIndex < 0) return NULL;
    _Q_ENTER_CRITICAL();
    qPriority_t MaxpValue = QUARKTS.QueueStack[0].Task->Priority;
    for(i=1;i<QUARKTS.QueueSize;i++){
        if(QUARKTS.QueueStack[i].Task == NULL) break;
        if(QUARKTS.QueueStack[i].Task->Priority > MaxpValue){
            MaxpValue = QUARKTS.QueueStack[i].Task->Priority;
            IndexTaskToExtract = i;
        }
    }   
    QUARKTS.EventInfo.EventData = QUARKTS.QueueStack[IndexTaskToExtract].QueueData;
    Task = QUARKTS.QueueStack[IndexTaskToExtract].Task;
    QUARKTS.QueueStack[IndexTaskToExtract].Task = NULL;  
    for(i=IndexTaskToExtract; i<QUARKTS.QueueIndex; i++) QUARKTS.QueueStack[i] = QUARKTS.QueueStack[i+1];    
    QUARKTS.QueueIndex--;    
    _Q_EXIT_CRITICAL();
    return Task;
}
/*============================================================================*/
void _qInitScheduler(qTime_t ISRTick, qTaskFcn_t IdleCallback, volatile qQueueStack_t *Q_Stack, uint8_t Size_Q_Stack){
    uint8_t i;
    QUARKTS.First = NULL;
    QUARKTS.Tick = ISRTick;
    QUARKTS.IDLECallback = IdleCallback;
    QUARKTS.ReleaseSchedCallback = NULL;
    QUARKTS.QueueStack = Q_Stack;
    QUARKTS.QueueSize = Size_Q_Stack;
    for(i=0;i<QUARKTS.QueueSize;i++) QUARKTS.QueueStack[i].Task = NULL;    
    QUARKTS.QueueIndex = -1;    
    QUARKTS.Flag.Init = qFalse;
    QUARKTS.Flag.ReleaseSched = qFalse;
    QUARKTS.Flag.FCallReleased = qFalse;
    QUARKTS.I_Restorer =  NULL;
    QUARKTS.I_Disable = NULL;
    _qSysTick_Epochs_ = 0;
}
/*============================================================================*/
/*int qSchedulerAddxTask(qTask_t *Task, qTaskFcn_t CallbackFcn, qPriority_t Priority, qTime_t Time, qIteration_t nExecutions, qState_t InitialState, void* arg)

Add a task to the scheduling scheme. The task is scheduled to run every <Time> 
seconds, <nExecutions> times and executing <CallbackFcb> method on every pass.

Parameters:
    - Task : A pointer to the task node.

    - CallbackFcn : A pointer to a void callback method with a qEvent_t parameter 
                 as input argument.

    - Priority : Task priority Value. [0(min) - 255(max)]

    - Time : Execution interval defined in seconds (floating-point format). 
               For inmediate execution (tValue = TIME_INMEDIATE).

    - nExecutions : Number of task executions (Integer value). For indefinite 
               execution (nExecutions = PERIODIC or INDEFINITE). Tasks do not 
               remember the number of iteration set initially. After the 
               iterations are done, internal iteration counter is 0. If you 
               need to perform another set of iterations, you need to set the 
               number of iterations again.
    >Note 1: Tasks which performed all their iterations put their own 
             state to DISABLED.
    >Note 2: Asynchronous triggers do not affect the iteration counter.

    - InitialState : Specifies the initial state of the task (qEnabled or qDisabled).

    - arg : Represents the task arguments. All arguments must be passed by
            reference and cast to (void *). Only one argument is allowed, 
            so, for multiple arguments, create a structure that contains 
            all of the arguments and pass a pointer to that structure.

Return value:

    Returns 0 on successs, otherwise returns -1;
    */
int qSchedulerAddxTask(qTask_t *Task, qTaskFcn_t CallbackFcn, qPriority_t Priority, qTime_t Time, qIteration_t nExecutions, qState_t InitialState, void* arg){
    if(Task==NULL) return -1;
    if (((Time/2)<QUARKTS.Tick && Time) || CallbackFcn == NULL) return -1;    
    Task->Callback = CallbackFcn;
    Task->Interval = (qClock_t)(Time/QUARKTS.Tick);
    Task->TaskData = arg;
    Task->Priority = Priority;
    Task->Iterations = nExecutions;    
    Task->Flag.AsyncRun = Task->Flag.InitFlag =  Task->Flag.RBAutoPop = Task->Flag.RBCount = Task->Flag.RBCount = Task->Flag.RBEmpty = qFalse ;
    Task->Flag.Enabled = (uint8_t)(InitialState != qFalse);
    Task->Next = QUARKTS.First;
    QUARKTS.First = Task;
    Task->Cycles = 0;
    Task->ClockStart = _qSysTick_Epochs_;
    QUARKTS.Flag.Init = 0;
    Task->RingBuff = NULL;
    Task->StateMachine = NULL;
    return 0;
}
/*============================================================================*/
/*int qSchedulerAddeTask(qTask_t *Task, qTaskFcn_t CallbackFcn, qPriority_t Priority, void* arg)

Add a task to the scheduling scheme.  This API creates a task with qDisabled 
state by default , so this task will be oriented to be executed only, when 
asynchronous events occurs. However, this behavior can be changed in execution
time using qTaskSetTime or qTaskSetIterations.

Parameters:

    - Task : A pointer to the task node.

    - CallbackFcn : A pointer to a void callback method with a qEvent_t parameter
                 as input argument.

    - Priority : Task priority Value. [0(min) - 255(max)]

    - arg :      Represents the task arguments. All arguments must be passed by
                 reference and cast to (void *). Only one argument is allowed, 
                 so, for multiple arguments, create a structure that contains 
                 all of the arguments and pass a pointer to that structure.
     
Return value:

    Returns 0 on successs, otherwise returns -1;     
     */
int qSchedulerAddeTask(qTask_t *Task, qTaskFcn_t CallbackFcn, qPriority_t Priority, void* arg){
    return qSchedulerAddxTask(Task, CallbackFcn, Priority, TIME_INMEDIATE, SINGLESHOT, 0, arg);
}
/*============================================================================*/
/*int qSchedulerAddSMTask(qTask_t *Task, qPriority_t Priority, qTime_t Time,
                         qSM_t *StateMachine, qSM_State_t InitState, 
                         qSM_ExState_t BeforeAnyState, qSM_ExState_t SuccessState,
                         qSM_ExState_t FailureState, qSM_ExState_t UnexpectedState,
                         qState_t InitialTaskState, void *arg)

Add a task to the scheduling scheme running a dedicated state-machine. 
The task is scheduled to run every <Time> seconds in PERIODIC mode. The event info
will be available as a generic pointer inside the <Data> field of the qSM_t pointer
passed as input argument inside every state.

Parameters:
    - Task : A pointer to the task node.

    - Priority : Task priority Value. [0(min) - 255(max)]

    - Time : Execution interval defined in seconds (floating-point format). 
               For inmediate execution (tValue = TIME_INMEDIATE).

    - StateMachine: A pointer to Finite State-Machine (FSM) object
  
    - InitState : The first state to be performed. This argument is a pointer 
                  to a callback function, returning qSM_Status_t and with a 
                  qSM_t pointer as input argument.

    - BeforeAnyState : A state called before the normal state machine execution.
                  This argument is a pointer to a callback function,  with a 
                  qSM_t pointer as input argument.
 
    - SuccessState : State performed after a state finish with return status 
                     qSM_EXIT_SUCCESS. This argument is a pointer to a callback
                     function with a qSM_t pointer as input argument.

    - FailureState : State performed after a state finish with return status 
                     qSM_EXIT_FAILURE. This argument is a pointer to a callback
                     function with a qSM_t pointer as input argument.

    - UnexpectedState : State performed after a state finish with return status
                        value between -32766 and 32767. This argument is a 
                        pointer to a callback function with a qSM_t pointer
                        as input argument.
 
    - InitialTaskState : Specifies the initial state of the task (qEnabled or qDisabled).

    - arg : Represents the task arguments. All arguments must be passed by
                     reference and cast to (void *). Only one argument is allowed, 
                     so, for multiple arguments, create a structure that contains 
                     all of the arguments and pass a pointer to that structure.
    Note:
 
Return value:

    Returns 0 on successs, otherwise returns -1;
    */
int qSchedulerAddSMTask(qTask_t *Task, qPriority_t Priority, qTime_t Time,
                                  qSM_t *StateMachine, qSM_State_t InitState, qSM_ExState_t BeforeAnyState, qSM_ExState_t SuccessState, qSM_ExState_t FailureState, qSM_ExState_t UnexpectedState,
                                  qState_t InitialTaskState, void *arg){
    if(StateMachine==NULL || InitState == NULL) return -1;
    if (qSchedulerAddxTask(Task, (qTaskFcn_t)1, Priority, Time, PERIODIC, InitialTaskState, arg) ==-1) return -1;
    Task->StateMachine = StateMachine;
    StateMachine->NextState = InitState;
    StateMachine->PreviousState = NULL;
    StateMachine->_.__Failure = FailureState;
    StateMachine->_.__Success = SuccessState;
    StateMachine->_.__Unexpected = UnexpectedState;
    StateMachine->_.__BeforeAnyState = BeforeAnyState; 
    return 0;
}
/*============================================================================*/
static void _qTriggerEvent(qTask_t *Task, qTrigger_t Event){
    if(Task==NULL) return; /*Not a valid task, do nothing*/
    /*Fill the event info structure*/
    QUARKTS.EventInfo.Trigger =  Event; 
    QUARKTS.EventInfo.FirstCall = (uint8_t)(!Task->Flag.InitFlag);    
    QUARKTS.EventInfo.TaskData = Task->TaskData;
    /*If the task has a FSM attached, just run it*/
    __qCurrExTask = Task;
    if (Task->StateMachine != NULL && Task->Callback==(qTaskFcn_t)1) qStateMachine_Run(Task->StateMachine, (void*)&QUARKTS.EventInfo);   
    else if (Task->Callback != NULL) Task->Callback((qEvent_t)&QUARKTS.EventInfo); /*else, just lauch the callback function*/        
    __qCurrExTask = NULL;
    if(Event==byRBufferPop) Task->RingBuff->tail++;  /*extract the data from the RBuffer, if the event wask byRBufferPop*/
    Task->Flag.InitFlag = qTrue; /*clear the init flag*/
    QUARKTS.EventInfo.EventData = NULL; /*clear the eventdata*/
    Task->Cycles++; /*increase the task cycles*/
}
/*============================================================================*/
static void _qTaskChainbyPriority(void){
    qTask_t *a = NULL, *b = NULL, *c = NULL, *e = NULL, *tmp = NULL; 
    qTask_t *head = QUARKTS.First;
    _Q_ENTER_CRITICAL();
    while(e != head->Next) {
        c = a = head;
        b = a->Next;
        while(a != e) {
            if(a->Priority < b->Priority) {
                tmp = b->Next;
                b->Next = a;
                if(a == head)  QUARKTS.First = head = b;
                else  c->Next = b;
                c = b;
                a->Next = tmp;
            } 
            else {
                c = a;
                a = a->Next;
            }
            b = a->Next;
            if(b == e) e = a;
        }
    }
    QUARKTS.Flag.Init= 1; /*set the initializtion flag*/
    _Q_EXIT_CRITICAL();
}
/*============================================================================*/
/*int qTaskLinkRBuffer(qTask_t *Task, qRBuffer_t *RingBuffer, qRBLinkMode_t Mode, uint8_t arg)

Links the Task with a Ring Buffer. 

Parameters:

    - Task : A pointer to the task node.

    - RingBuffer : A pointer to a Ring Buffer object
 
    - Mode: Linking mode. This implies the event that will trigger the task according
            to one of the following modes:
                        > RB_AUTOPOP: The task will be triggered if there is elements 
                          in the Ring Buffer. Data data will be popped
                          automatically in every trigger and will be available 
                          in the <EventData> field of qEvent_t structure.
     
                        > RB_FULL: the task will be triggered if the Ring Buffer
                          is full. The pointer to the RingBuffer will be 
                          available in the <EventData> field of qEvent_t structure.

                        > RB_COUNT: the task will be triggered if the count of 
                          elements in the Ring Buffer reach the specified value. 
                          The pointer to the RingBuffer will be available in the
                          <EventData> field of qEvent_t structure.

                        > RB_EMPTY: the task will be triggered if the Ring Buffer
                          is empty. The pointer to the RingBuffer will be 
                          available in the <EventData> field of qEvent_t structure.
 
    - arg: This argument defines if the Ring buffer will be linked (qLINK) or 
           unlinked (qUNLINK) from the task.
           If the RB_COUNT mode is specified, this value will be used to check
           the element count of the Ring Buffer. A zero value will act as 
           an unlink action. 

Return value:

    Returns 0 on successs, otherwise returns -1;     
     */
int qTaskLinkRBuffer(qTask_t *Task, qRBuffer_t *RingBuffer, qRBLinkMode_t Mode, uint8_t arg){
    if(RingBuffer == NULL || Task==NULL) return -1;
    if(RingBuffer->data == NULL) return -1;    
    switch(Mode){
        case RB_AUTOPOP:
            Task->Flag.RBAutoPop = (qBool_t)(arg!=qFalse);
            break;
        case RB_FULL:
            Task->Flag.RBFull = (qBool_t)(arg!=qFalse);
            break;
        case RB_COUNT:
            Task->Flag.RBCount = arg;
            break;
        case RB_EMPTY:
            Task->Flag.RBEmpty = (qBool_t)(arg!=qFalse);
            break;
        default: return -1;
    }
    Task->RingBuff = (arg>0)? RingBuffer : NULL;
    return 0;
}
/*============================================================================*/
static qTrigger_t _qCheckRBufferEvents(qTask_t *Task){
    if(Task==NULL) return _Q_NO_VALID_TRIGGER_;
    qRBuffer_t *rb = Task->RingBuff;
    void* popdata = NULL;
    if(rb == NULL) return _Q_NO_VALID_TRIGGER_;
    if(Task->Flag.RBFull){
        if(_qRBufferFull(rb)){
            QUARKTS.EventInfo.EventData = (void*)rb; 
            return byRBufferFull;
        }
    }
    if(Task->Flag.RBCount>0){
        if( Task->Flag.RBCount >= _qRBufferCount(rb) ){
            QUARKTS.EventInfo.EventData = (void*)rb;        
            return byRBufferCount;    
        } 
    }
    if(Task->Flag.RBAutoPop){
        if((popdata = qRBufferGetFront(rb))!=NULL){
            QUARKTS.EventInfo.EventData = popdata; 
            return byRBufferPop;
        }
    }
    if(Task->Flag.RBEmpty){
        if (qRBufferEmpty(rb)){
            QUARKTS.EventInfo.EventData = (void*)rb; 
            return byRBufferEmpty;
        }
    }
    return _Q_NO_VALID_TRIGGER_;
}
/*============================================================================*/
static void _qTriggerReleaseSchedEvent(void){
    QUARKTS.Flag.Init = qFalse;
    QUARKTS.Flag.ReleaseSched = qFalse;
    QUARKTS.EventInfo.FirstCall = (uint8_t)(!QUARKTS.Flag.FCallReleased);
    QUARKTS.EventInfo.Trigger = byAsyncEvent;
    if(QUARKTS.ReleaseSchedCallback!=NULL) QUARKTS.ReleaseSchedCallback((qEvent_t)&QUARKTS.EventInfo);
    QUARKTS.Flag.FCallIdle = qTrue;      
}
/*============================================================================*/
static void _qTriggerIdleTask(void){
    QUARKTS.EventInfo.FirstCall = (uint8_t)(!QUARKTS.Flag.FCallIdle);
    QUARKTS.EventInfo.Trigger = byPriority;
    QUARKTS.IDLECallback((qEvent_t)&QUARKTS.EventInfo);
    QUARKTS.Flag.FCallIdle = qTrue;      
}
/*============================================================================*/
/*
void qSchedulerSysTick(void)

Feed the scheduler system tick. This call is mandatory and must be called once
inside the dedicated timer interrupt service routine (ISR). 
*/    
void qSchedulerSysTick(void){_qSysTick_Epochs_++;}
/*============================================================================*/
/*void qSchedule(void)
    
Executes the task-scheduler scheme. It must be called once after the task
pool has been defined.

  Note : qScheduleRun keeps the application in an endless loop
*/
void qSchedulerRun(void){
    qTask_t *Task, *qTask; /*Current task in the chain, extracted task from queue if available*/
    qTrigger_t trg = _Q_NO_VALID_TRIGGER_;
    _Q_MAIN_SCHEDULE(QUARKTS); /*Scheduling start-point*/
    if(!QUARKTS.Flag.Init)  _qTaskChainbyPriority(); /*if initial scheduling conditions changed, sort the chain by priority (init flag internally set)*/  
    for(Task = QUARKTS.First; Task != NULL; Task = Task->Next){  /*Loop every task in the linked-chain*/
        if ((qTask = _qPrioQueueExtract())!=NULL)  _qTriggerEvent(qTask, byQueueExtraction); /*Available queueded task always will be executed in every chain sweep*/ 
        if( _Q_TASK_DEADLINE_REACHED(Task) && _Q_TASK_HAS_PENDING_ITERS(Task) && qTaskIsEnabled(Task)){ /*Check if task is enabled and reach the time-deadline*/
            Task->ClockStart = _qSysTick_Epochs_; /*Restart the time*/
            if(Task->Iterations!= PERIODIC) Task->Iterations--; /*Decrease the iteration value*/
            if(Task->Iterations == 0) Task->Flag.Enabled = qFalse; /*When the iteration value is reached, the task will be disabled*/
            _qTriggerEvent(Task, byTimeElapsed); /*Launch the task with the corresponding event*/       
        }
        else if((trg=_qCheckRBufferEvents(Task)) != _Q_NO_VALID_TRIGGER_) /*If the deadline has not met, check if there is a RBuffer event available*/
            _qTriggerEvent(Task, trg); /*If a RBuffer event exist, the flag will be available in the <trg> variable, so lauch the task with the corresponding event*/                  
        else if( Task->Flag.AsyncRun){ /*The last check will be if the task has an async event*/
            QUARKTS.EventInfo.EventData = Task->AsyncData; /*Transfer async data to the eventinfo structure*/
            Task->Flag.AsyncRun = qFalse; /*Clear the async flag*/
            _qTriggerEvent(Task, byAsyncEvent); /*Launch the task with the corresponding event*/
        }
        else if( QUARKTS.IDLECallback!= NULL) _qTriggerIdleTask(); /*if the current task has no pending events, launch the IDLETask*/
    }
    _Q_RESUME_SCHEDULE(_qTriggerReleaseSchedEvent); /*scheduling end-point (also check for scheduling-release request)*/
}
/*============================================================================*/
/*int qStateMachine_Init(qSM_t *obj, qSM_State_t InitState, qSM_ExState_t SuccessState, qSM_ExState_t FailureState, qSM_ExState_t UnexpectedState);

Initializes a finite state machine (FSM).

Parameters:

    - obj : a pointer to the FSM object.

    - InitState : The first state to be performed. This argument is a pointer 
                  to a callback function, returning qSM_Status_t and with a 
                  qFSM_t pointer as input argument.

    - SuccessState : State performed after a state finish with return status 
                     qSM_EXIT_SUCCESS. This argument is a pointer to a callback
                     function with a qFSM_t pointer as input argument.

    - FailureState : State performed after a state finish with return status 
                     qSM_EXIT_FAILURE. This argument is a pointer to a callback
                     function with a qFSM_t pointer as input argument.

    - UnexpectedState : State performed after a state finish with return status
                        value between -32766 and 32767. This argument is a 
                        pointer to a callback function with a qFSM_t pointer
                        as input argument.

Return value:

    Returns 0 on successs, otherwise returns -1;
*/
int qStateMachine_Init(qSM_t *obj, qSM_State_t InitState, qSM_ExState_t SuccessState, qSM_ExState_t FailureState, qSM_ExState_t UnexpectedState){
    if(obj==NULL || InitState == NULL) return -1;
    obj->NextState = InitState;
    obj->PreviousState = NULL;
    obj->_.__Failure = FailureState;
    obj->_.__Success = SuccessState;
    obj->_.__Unexpected = UnexpectedState;
    return 0;
}
/*============================================================================*/
/*void qStateMachine_Run(qSM_t *obj, void* Data)

Execute the Finite State Machine (FSM).

Parameters:

    - obj : a pointer to the FSM object.

    - Data : Represents the FSM arguments. All arguments must be passed by 
             reference and cast to (void *). Only one argument is allowed, so,
             for multiple arguments, create a structure that contains all of 
             the arguments and pass a pointer to that structure.
*/    
void qStateMachine_Run(qSM_t *obj, void *Data){
    if(obj == NULL) return;
    qSM_State_t prev  = NULL;
    obj->Data = Data;
    if(obj->_.__BeforeAnyState != NULL) obj->_.__BeforeAnyState(obj);
    if(obj->NextState!=NULL){
        obj->StateJustChanged = (qBool_t)(obj->PreviousState != obj->NextState);
        prev = obj->NextState;
        obj->PreviousReturnStatus = obj->NextState(obj);
        obj->PreviousState = prev;
    }
    else    obj->PreviousReturnStatus = qSM_EXIT_FAILURE;
    
    switch(obj->PreviousReturnStatus){
        case qSM_EXIT_FAILURE:           
            if(obj->_.__Failure != NULL) obj->_.__Failure(obj);
            break;
        case qSM_EXIT_SUCCESS:
            if(obj->_.__Success != NULL) obj->_.__Success(obj);
            break;
        default:
            if(obj->_.__Unexpected != NULL) obj->_.__Unexpected(obj);
            break;
    }
 }
/*============================================================================*/
/*int qSTimerSet(qSTimer_t OBJ, qTime_t Time)
 
Set the expiration time for a STimer. On success, the Stimer gets 
armed immediately

Parameters:

    - obj : A pointer to the STimer object.

    - Time : The expiration time(Must be specified in seconds).

    > Note 1: The scheduler must be running before using STimers.
    > Note 2: The expiration time should be at least, two times greather than 
              the scheduler-Tick.

Return value:

    Returns qFalse on success, otherwise, returns qError.
*/ 
qBool_t qSTimerSet(qSTimer_t *obj, qTime_t Time){
    if(obj==NULL) return qError;
    if ( (Time/2.0)<QUARKTS.Tick ) return qError;
    obj->TV = (qClock_t)(Time/QUARKTS.Tick);
    obj->Start = _qSysTick_Epochs_;
    obj->SR = qTrue;
    return qFalse;
}
/*============================================================================*/
/*unsigned char qSTimerFreeRun(qSTimer_t *obj, qTime_t Time)

Non-Blocking STimer check with automatic arming

Parameters:

    - obj : A pointer to the STimer object.
  
    - Time : The expiration time(Must be specified in seconds).
 
    > Note 1: The scheduler must be running before using STimers.
    > Note 2: The expiration time should be at least, two times greather than  
              the scheduler-Tick.
    > Note 3: Time parameter is only taken when the STimer is re-armed
  
Return value:

    Returns qTrue when STimer expires, otherwise, returns qFalse.
    > Note 4: A disarmed STimer also returns qFalse.
    > Note 5: After the STimer expiration,  qSTimerFreeRun re-arms the STimer
*/
qBool_t qSTimerFreeRun(qSTimer_t *obj, qTime_t Time){
    if(obj==NULL) return qError;
    if(obj->SR){
        if (qSTimerExpired(obj)){
            qSTimerDisarm(obj);
            return qTrue;
        }
        else return qFalse;
    }
    return qSTimerSet(obj, Time);    
}
/*============================================================================*/
/*unsigned char qSTimerExpired(qSTimer_t *obj)

Non-Blocking Stimer check

Parameters:

    - obj : A pointer to the STimer object.

Return value:

    Returns qTrue when STimer expires, otherwise, returns qFalse.
    > Note 1: A disarmed STimer also returns false.

*/
qBool_t qSTimerExpired(qSTimer_t *obj){
    if(obj==NULL) return qError;
    if(!obj->SR) return qFalse; 
    return (qBool_t)((_qSysTick_Epochs_ - obj->Start)>=obj->TV);
}
/*============================================================================*/
/*qTime_t qSTimerElapsed(qSTimer_t *obj)

Query the elapsed time

Parameters:

    - obj : A pointer to the STimer object.

Return value:

    The Elapsed time specified in epochs
*/
qClock_t qSTimerElapsed(qSTimer_t *obj){
    if(obj==NULL) return 0;
    return (_qSysTick_Epochs_-obj->Start);
}
/*============================================================================*/
/*qClock_t qSTimerRemainingEpochs(qSTimer_t *obj)

Query the remaining epochs

Parameters:

    - obj : A pointer to the STimer object.

Return value:

    The remaining time specified in epochs
*/
qClock_t qSTimerRemaining(qSTimer_t *obj){
    if(obj==NULL) return 0;
    qClock_t elapsed = qSTimerElapsed(obj);
    return (obj->TV <= 0 || elapsed>obj->TV)? obj->TV : obj->TV-elapsed;
}
/*============================================================================*/
/*void qSTimerDisarm(qSTimer_t *obj)

Disarms the STimer

Parameters:

    - obj : A pointer to the STimer object.  
*/
void qSTimerDisarm(qSTimer_t *obj){
    if(obj==NULL) return;
    obj->SR = qFalse;
    obj->Start = 0;
}
#ifdef QMEMORY_MANAGER
/*============================================================================*/
/*void* qMemoryAlloc(qMemoryPool_t *obj, uint16_t size)
 
Allocate the required memory from the specified memory heap. The allocation 
is rounded to the memory block size.
 
Parameters:

    - obj : a pointer to the memory pool object
 
    - size : amount of memory to allocate
 
Return value:

    A pointer to allocated memory or null in there is no avaible memory
 */
void* qMemoryAlloc(qMemoryPool_t *obj, qSize_t size){
    if(obj==NULL) return NULL;
    uint8_t i, j, k, c;
    uint16_t sum;		
    uint8_t *offset = obj->Blocks;			
    j = 0;	
    _Q_ENTER_CRITICAL();
    while( j < obj->NumberofBlocks ) {			
        sum  = 0;
	i = j;
	while( i < obj->NumberofBlocks ) {		
            if( *(obj->BlockDescriptors+i) ) {
                offset += (*(obj->BlockDescriptors+i)) * (obj->BlockSize);
		i += *(obj->BlockDescriptors+i);				 
		continue;
            }
            break;				
	}
	j = i;	
	for(k = 1, i = j; i < obj->NumberofBlocks; k++, i++) {
            if( *(obj->BlockDescriptors+i) ) {				
                j = i + *(obj->BlockDescriptors+i);
		offset = (uint8_t*) obj->Blocks;
		offset += j * (obj->BlockSize);
		break;
            }
            sum += obj->BlockSize;
            if( sum >= size ) {
                *(obj->BlockDescriptors+j) = k;
		for(c=0;c<size;c++) offset[i] = 0x00u;/*zero-initialized memory block*/ 
                _Q_EXIT_CRITICAL();
		return (void*)offset;
            }						
	}
	if( i == obj->NumberofBlocks ) break;
    }
    _Q_EXIT_CRITICAL();
    return NULL; /*memory not available*/
}
/*============================================================================*/
/*void* qMemoryFree(qMemoryPool_t *obj, void* pmem)
 
Free the previously allocated memory by returning it to the specified memory pool.
 
Parameters:

    - obj : a pointer to the memory pool object
 
    - pmem : pointer to the previously allocated memory
 
Note: The memory must be returned to the pool from where was allocated
 */
void qMemoryFree(qMemoryPool_t *obj, void* pmem){
    if(obj==NULL || pmem==NULL) return;
    uint8_t i, *p;
    _Q_ENTER_CRITICAL();	
    p = (uint8_t*) obj->Blocks;
    for(i = 0; i < obj->NumberofBlocks; i++) {
        if( p == pmem ) {
            *(obj->BlockDescriptors + i) = 0;
            break;
	}
	p += obj->BlockSize;
    }
    _Q_EXIT_CRITICAL();
}
/*============================================================================*/
#endif

/*============================================================================*/
static qSize_t _qRBufferValidPowerOfTwo(qSize_t k){
    uint16_t i;
    if ( ((k-1) & k) != 0) {
        k--;
        for (i = 1; i<sizeof(uint16_t)*8; i=i*2)  k = k|k>>i;
        k = (k+1)>>1;
    }
    return k;
}
/*============================================================================*/
static qSize_t _qRBufferCount(qRBuffer_t *obj){
    return (obj ? (obj->head - obj->tail) : 0);
}
/*============================================================================*/
static qBool_t _qRBufferFull(qRBuffer_t *obj){
    return (qBool_t)(obj ? (_qRBufferCount(obj) == obj->Elementcount) : qTrue);
}
/*============================================================================*/
/*void qRBufferInit(qRBuffer_t *obj, void* DataBlock, uint16_t ElementSize, uint16_t ElementCount)
 
Configures the ring buffer
 
Parameters:

    - obj : a pointer to the Ring Buffer object
 
    - DataBlock :  data block or array of data

    - ElementSize : size of one element in the data block
 
    - ElementCount : Max number of elements in the bufffer
 
Note: Element_count should be a power of two, or it will only use the next 
      lower power of two
 */
void qRBufferInit(qRBuffer_t *obj, void* DataBlock, qSize_t ElementSize, qSize_t ElementCount){
    if(obj==NULL || DataBlock==NULL) return;
    obj->head = 0;
    obj->tail = 0;
    obj->data = DataBlock;
    obj->ElementSize = ElementSize;
    obj->Elementcount = _qRBufferValidPowerOfTwo(ElementCount);     
}
/*============================================================================*/
/*qBool_t qRBufferEmpty(qRBuffer_t *obj)
 
Returns the empty/full status of the ring buffer
 
Parameters:

    - obj : a pointer to the Ring Buffer object
  
Return value:

    qTrue if the ring buffer is empty, qFalse if it is not.
 */
qBool_t qRBufferEmpty(qRBuffer_t *obj){
    if(obj==NULL) return qError;
    return (qBool_t)(obj ? (_qRBufferCount(obj) == 0) : qTrue);    
}
/*============================================================================*/
/*void* qRBufferGetFront(qRBuffer_t *obj)
 
Looks at the data from the head of the list without removing it
 
Parameters:

    - obj : a pointer to the Ring Buffer object
  
Return value:

    Pointer to the data, or NULL if nothing in the list
 */
void* qRBufferGetFront(qRBuffer_t *obj){
    if (obj==NULL) return NULL;
    return (void*)(!qRBufferEmpty(obj) ? &(obj->data[(obj->tail % obj->Elementcount) * obj->ElementSize]) : NULL);    
}
/*============================================================================*/
/*void* qRBufferPopFront(qRBuffer_t *obj)
 
Extract the data from the front of the list, and removes it
 
Parameters:

    - obj : a pointer to the Ring Buffer object
    - dest: pointer to where the data will be written
  
Return value:

    qTrue if data was retrived from Rbuffer, otherwise returns qFalse
*/
void* qRBufferPopFront(qRBuffer_t *obj, void *dest){
    if(obj == NULL) return NULL;
    void *data = NULL;
    if (!qRBufferEmpty(obj)) {
        data = (void*)(&(obj->data[(obj->tail % obj->Elementcount) * obj->ElementSize]));
        memcpy(dest, data, obj->ElementSize);
        obj->tail++;
    }
    return data;    
}
/*============================================================================*/
/*qBool_t qRBufferPush(qRBuffer_t *obj, void *data)
 
Adds an element of data to the ring buffer
 
Parameters:

    - obj : a pointer to the Ring Buffer object
    - data : a pointer to the element who needs to be added to the ring-buffer
  
Return value:

    qTrue on succesful add, qFalse if not added
*/
qBool_t qRBufferPush(qRBuffer_t *obj, void *data){
    if(obj==NULL) return qFalse;
    qBool_t status = qFalse;
    uint8_t *data_element = (uint8_t*)data;
    volatile uint8_t *ring_data = NULL;
    uint16_t i;

    if(obj && data_element){
        if(!_qRBufferFull(obj)){
            ring_data = obj->data + ((obj->head % obj->Elementcount) * obj->ElementSize);
            for (i = 0; i < obj->ElementSize; i++) ring_data[i] = data_element[i];            
            obj->head++;
            status = qTrue;
        }
    }
    return status;    
}
/*============================================================================*/
