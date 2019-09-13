//
// Virual Memory Simulator
// Two-level page table system
// Inverted page table with a hashing system
// Student Name: 신재협
//
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define PAGESIZEBITS 12	// page size = 4Kbytes
#define VIRTUALADDRBITS 32 // virtual address space size = 4Gbytes

struct pageTableEntry
{
	int level; // page table level (1 or 2)
	char valid;
	struct pageTableEntry *secondLevelPageTable; // valid if this entry is for the first level page table (level = 1)
	int frameNumber;							 // valid if this entry is for the second level page table (level = 2)
};

struct framePage
{
	int number;					// frame number
	int pid;					// Process id that owns the frame
	int virtualPageNumber;		// virtual page number using the frame
	struct framePage *lruLeft;  // for LRU circular doubly linked list
	struct framePage *lruRight; // for LRU circular doubly linked list
};

struct invertedPageTableEntry
{
	int pid;			   // process id
	int virtualPageNumber; // virtual page number
	int frameNumber;	   // frame number allocated
	struct invertedPageTableEntry *next;
};

struct procEntry
{
	char *traceName;		  // the memory trace name
	int pid;				  // process (trace) id
	int ntraces;			  // the number of memory traces
	int num2ndLevelPageTable; // The 2nd level page created(allocated);
	int numIHTConflictAccess; // The number of Inverted Hash Table Conflict Accesses
	int numIHTNULLAccess;	 // The number of Empty Inverted Hash Table Accesses
	int numIHTNonNULLAcess;   // The number of Non Empty Inverted Hash Table Accesses
	int numPageFault;		  // The number of page faults
	int numPageHit;			  // The number of page hits
	struct pageTableEntry *firstLevelPageTable;
	FILE *tracefp;
};

struct framePage *oldestFrame; // the oldest frame pointer 교체 대상

int firstLevelBits, phyMemSizeBits, numProcess;

void initPhyMem(struct framePage *phyMem, int nFrame)
{
	int i;

	for (i = 0; i < nFrame; i++)
	{
		phyMem[i].number = i;
		phyMem[i].pid = -1;
		phyMem[i].virtualPageNumber = -1;
		phyMem[i].lruLeft = &phyMem[(i - 1 + nFrame) % nFrame];
		phyMem[i].lruRight = &phyMem[(i + 1 + nFrame) % nFrame];
	}

	oldestFrame = &phyMem[0];
}

void secondLevelVMSim(struct procEntry *procTable, struct framePage *phyMemFrames)
{
	int i, j;
	unsigned addr;
	char rw;
	FILE *file = NULL;

	for (i = 0; i < numProcess; i++)
	{
		if ((file = fopen(procTable[i].traceName, "r")) == NULL)
		{
			printf("File Open Error!\n");
			exit(0);
		}

		procTable[i].tracefp = file;

		//1st페이지 테이블 생성. 엔트리 수는 2의 1stLVbits제곱.
		procTable[i].firstLevelPageTable = (struct pageTableEntry *)malloc(sizeof(struct pageTableEntry) * (1 << firstLevelBits));
		for (j = 0; j < (1 << firstLevelBits); j++)
		{
			procTable[i].firstLevelPageTable[j].level = 1;
			procTable[i].firstLevelPageTable[j].valid = 0;
			procTable[i].firstLevelPageTable[j].frameNumber = -1;
			procTable[i].firstLevelPageTable[j].secondLevelPageTable = NULL;
		}
	}

	int b_cnt = 0;
	int *b_numprocess = (int *)malloc(sizeof(int) * numProcess);
	for (i = 0; i < numProcess; i++)
	{
		b_numprocess[i] = 0;
	}
	i = 0;
	while (b_cnt < numProcess)
	{
		if (fscanf(procTable[i].tracefp, "%x %c", &addr, &rw) > 0)
		{
			//1st page number의미하는 pnum1 위해 addr를 오른쪽 비트연산(새로 채워지는 비트=0)
			unsigned int pnum1 = (addr >> (VIRTUALADDRBITS - firstLevelBits));
			//2nd page number
			unsigned int pnum2 = (addr << (firstLevelBits));
			pnum2 = (pnum2 >> (VIRTUALADDRBITS - firstLevelBits));
			unsigned int pagenum = (addr >> PAGESIZEBITS);
			unsigned int offset = (addr << (VIRTUALADDRBITS - PAGESIZEBITS));
			offset = (offset >> (VIRTUALADDRBITS - PAGESIZEBITS));
			//frame number
			unsigned int fnum;
			unsigned physicalAddr;

			//1st page table의 2nd page table 존재
			if (procTable[i].firstLevelPageTable[pnum1].valid == 1)
			{
				//2nd PT에 Hit
				if (procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].valid == 1 && procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].frameNumber != -1)
				{
					procTable[i].numPageHit++;
					fnum = procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].frameNumber;

					//lru적용
					phyMemFrames[fnum].lruLeft->lruRight = phyMemFrames[fnum].lruRight;
					phyMemFrames[fnum].lruRight->lruLeft = phyMemFrames[fnum].lruLeft;

					phyMemFrames[fnum].lruLeft = oldestFrame->lruLeft;
					oldestFrame->lruLeft->lruRight = &phyMemFrames[fnum];
					phyMemFrames[fnum].lruRight = oldestFrame;
					oldestFrame->lruLeft = &phyMemFrames[fnum];
				}
				// 2nd PT에 Fault이므로 LRU적용
				else
				{
					procTable[i].numPageFault++;

					//기존 할당되어 있던 매핑정보 삭제
					if (oldestFrame->virtualPageNumber != -1)
					{
						unsigned int t_pnum1 = (oldestFrame->virtualPageNumber >> (VIRTUALADDRBITS - firstLevelBits - PAGESIZEBITS));
						unsigned int t_pnum2 = (oldestFrame->virtualPageNumber << (firstLevelBits + PAGESIZEBITS));
						t_pnum2 = (t_pnum2 >> (firstLevelBits + PAGESIZEBITS));
						if (procTable[oldestFrame->pid].firstLevelPageTable[t_pnum1].valid == 1)
						{
							procTable[oldestFrame->pid].firstLevelPageTable[t_pnum1].secondLevelPageTable[t_pnum2].valid = 0;
							procTable[oldestFrame->pid].firstLevelPageTable[t_pnum1].secondLevelPageTable[t_pnum2].frameNumber = -1;
						}
					}

					procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].level = 2;
					procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].valid = 1;

					procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].frameNumber = oldestFrame->number;
					oldestFrame->virtualPageNumber = pagenum;
					oldestFrame->pid = procTable[i].pid;

					struct framePage *tmpFrame;
					tmpFrame = oldestFrame;
					oldestFrame = oldestFrame->lruRight;
					oldestFrame->lruLeft->lruRight = tmpFrame;
					oldestFrame->lruLeft = tmpFrame;

					fnum = oldestFrame->number;
				}
			}
			else //없다면 2nd PT 생성
			{
				procTable[i].numPageFault++;

				//기존 할당되어 있던 매핑정보 삭제
				if (oldestFrame->virtualPageNumber != -1)
				{
					unsigned int t_pnum1 = (oldestFrame->virtualPageNumber >> (VIRTUALADDRBITS - firstLevelBits - PAGESIZEBITS));
					unsigned int t_pnum2 = (oldestFrame->virtualPageNumber << (firstLevelBits + PAGESIZEBITS));
					t_pnum2 = (t_pnum2 >> (firstLevelBits + PAGESIZEBITS));
					if (procTable[oldestFrame->pid].firstLevelPageTable[t_pnum1].valid == 1)
					{
						procTable[oldestFrame->pid].firstLevelPageTable[t_pnum1].secondLevelPageTable[t_pnum2].valid = 0;
						procTable[oldestFrame->pid].firstLevelPageTable[t_pnum1].secondLevelPageTable[t_pnum2].frameNumber = -1;
					}
				}

				procTable[i].firstLevelPageTable[pnum1].valid = 1;
				//2nd페이지 테이블 생성. 엔트리 수는 2의 2ndLVbits제곱.
				procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable = (struct pageTableEntry *)malloc(sizeof(struct pageTableEntry) * (1 << (32 - firstLevelBits - PAGESIZEBITS)));
				procTable[i].num2ndLevelPageTable++;
				procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].level = 2;
				procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].valid = 1;

				//LRU적용
				procTable[i].firstLevelPageTable[pnum1].secondLevelPageTable[pnum2].frameNumber = oldestFrame->number;
				oldestFrame->virtualPageNumber = pagenum;
				oldestFrame->pid = procTable[i].pid;

				struct framePage *tmpFrame;
				tmpFrame = oldestFrame;
				oldestFrame = oldestFrame->lruRight;
				oldestFrame->lruLeft->lruRight = tmpFrame;
				oldestFrame->lruLeft = tmpFrame;

				fnum = oldestFrame->number;
			}
			procTable[i].ntraces++;
			physicalAddr = (unsigned)offset | (unsigned)(fnum << PAGESIZEBITS);
			printf("2Level procID %d traceNumber %d virtual addr %x pysical addr %x\n", i,
				   procTable[i].ntraces, addr, physicalAddr);
		}
		else if (b_numprocess[i] == 0)
		{
			b_cnt++;
			b_numprocess[i] = 1;
		}
		i++;
		if (i >= numProcess)
		{
			i = 0;
		}
	}

	for (i = 0; i < numProcess; i++)
	{
		free(procTable[i].firstLevelPageTable);
	}
	free(b_numprocess);

	//시뮬레이션 완료 후, 결과 출력
	for (i = 0; i < numProcess; i++)
	{
		printf("**** %s *****\n", procTable[i].traceName);
		printf("Proc %d Num of traces %d\n", i, procTable[i].ntraces);
		printf("Proc %d Num of second level page tables allocated %d\n", i, procTable[i].num2ndLevelPageTable);
		printf("Proc %d Num of Page Faults %d\n", i, procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n", i, procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
	}
	fclose(file);
}

void invertedPageVMSim(struct procEntry *procTable, struct framePage *phyMemFrames, int nFrame)
{
	int i, j;
	unsigned addr;
	char rw;
	FILE *file = NULL;
	struct invertedPageTableEntry *nextEntry;
	struct invertedPageTableEntry *prevEntry;
	struct invertedPageTableEntry *temp1;

	//물리메모리 프레임 수 크기의 global IPT 생성
	struct invertedPageTableEntry *invertedPT = (struct invertedPageTableEntry *)malloc(sizeof(struct invertedPageTableEntry) * nFrame);

	for (j = 0; j < nFrame; j++)
	{
		invertedPT[j].frameNumber = -1;
		invertedPT[j].pid = -1;
		invertedPT[j].virtualPageNumber = -1;
		invertedPT[j].next = NULL;
	}

	for (i = 0; i < numProcess; i++)
	{
		if ((file = fopen(procTable[i].traceName, "r")) == NULL)
		{
			printf("File Open Error!\n");
			exit(0);
		}

		procTable[i].tracefp = file;
	}

	int b_cnt = 0;
	int *b_numprocess = (int *)malloc(sizeof(int) * numProcess);
	for (i = 0; i < numProcess; i++)
	{
		b_numprocess[i] = 0;
	}
	i = 0;
	while (b_cnt < numProcess)
	{
		if (fscanf(procTable[i].tracefp, "%x %c", &addr, &rw) > 0)
		{
			//page number
			unsigned int pnum = (addr >> (PAGESIZEBITS));
			unsigned int offset = (addr << (32 - PAGESIZEBITS));
			offset = (offset >> (32 - PAGESIZEBITS));
			//frame number
			unsigned int fnum;
			unsigned physicalAddr;

			int b_search = 0;

			//해당하는 pid와 page number 찾기 위해 IPT탐색
			j = (pnum + procTable[i].pid) % nFrame; //Hash Table 인덱스 j

			//hit
			if (invertedPT[j].pid == procTable[i].pid && invertedPT[j].virtualPageNumber == pnum && phyMemFrames[invertedPT[j].frameNumber].virtualPageNumber == pnum && phyMemFrames[invertedPT[j].frameNumber].pid == procTable[i].pid)
			{
				procTable[i].numIHTConflictAccess++;
				procTable[i].numIHTNonNULLAcess++;
				b_search = 1;

				//lru적용
				phyMemFrames[invertedPT[j].frameNumber].lruLeft->lruRight = phyMemFrames[invertedPT[j].frameNumber].lruRight;
				phyMemFrames[invertedPT[j].frameNumber].lruRight->lruLeft = phyMemFrames[invertedPT[j].frameNumber].lruLeft;

				phyMemFrames[invertedPT[j].frameNumber].lruLeft = oldestFrame->lruLeft;
				oldestFrame->lruLeft->lruRight = &phyMemFrames[invertedPT[j].frameNumber];
				phyMemFrames[invertedPT[j].frameNumber].lruRight = oldestFrame;
				oldestFrame->lruLeft = &phyMemFrames[invertedPT[j].frameNumber];

				fnum = invertedPT[j].frameNumber;
			}
			else
			{
				//해쉬테이블 엔트리가 비어있음
				if (invertedPT[j].pid == -1)
				{
					procTable[i].numIHTNULLAccess++;
				}
				//해쉬테이블 엔트리가 차 있음
				else
				{
					procTable[i].numIHTNonNULLAcess++;
					procTable[i].numIHTConflictAccess++;

					nextEntry = &invertedPT[j];
					prevEntry = nextEntry;
					nextEntry = nextEntry->next;
					while (nextEntry != NULL)
					{
						procTable[i].numIHTConflictAccess++;

						//hit
						if (nextEntry->pid == procTable[i].pid && nextEntry->virtualPageNumber == pnum)
						{
							b_search = 1;
							fnum = nextEntry->frameNumber;

							//lru 적용
							phyMemFrames[invertedPT[j].frameNumber].lruLeft->lruRight = phyMemFrames[invertedPT[j].frameNumber].lruRight;
							phyMemFrames[invertedPT[j].frameNumber].lruRight->lruLeft = phyMemFrames[invertedPT[j].frameNumber].lruLeft;

							phyMemFrames[invertedPT[j].frameNumber].lruLeft = oldestFrame->lruLeft;
							oldestFrame->lruLeft->lruRight = &phyMemFrames[invertedPT[j].frameNumber];
							phyMemFrames[invertedPT[j].frameNumber].lruRight = oldestFrame;
							oldestFrame->lruLeft = &phyMemFrames[invertedPT[j].frameNumber];

							break;
						}

						if (nextEntry->next == NULL)
						{
							break;
						}
						prevEntry = nextEntry;
						nextEntry = nextEntry->next;
					}
				}
			}

			if (b_search == 1) //hit
			{
				procTable[i].numPageHit++;
			}
			else //fault
			{
				procTable[i].numPageFault++;

				//lru를 이용하여 ipt에 추가
				if (invertedPT[j].pid == -1) //엔트리 리스트 노드 0개
				{
					int k;
					k = (oldestFrame->virtualPageNumber + oldestFrame->pid) % nFrame;

					if (oldestFrame->pid != -1)
					{
						if (oldestFrame->pid == invertedPT[k].pid && oldestFrame->virtualPageNumber == invertedPT[k].virtualPageNumber)
						{
							if (invertedPT[k].next != NULL)
							{
								invertedPT[k].frameNumber = invertedPT[k].next->frameNumber;
								invertedPT[k].pid = invertedPT[k].next->pid;
								invertedPT[k].virtualPageNumber = invertedPT[k].next->virtualPageNumber;
								invertedPT[k].next = invertedPT[k].next->next;
							}
							else
							{
								invertedPT[k].frameNumber = -1;
								invertedPT[k].pid = -1;
								invertedPT[k].virtualPageNumber = -1;
								invertedPT[k].next = NULL;
							}
						}
						else
						{
							nextEntry = &invertedPT[k];
							prevEntry = nextEntry;
							nextEntry = nextEntry->next;
							while (nextEntry != NULL)
							{
								if (oldestFrame->pid == invertedPT[k].pid && oldestFrame->virtualPageNumber == invertedPT[k].virtualPageNumber)
								{
									prevEntry->next = nextEntry->next;
									free(nextEntry);
									break;
								}

								if (nextEntry->next == NULL)
								{
									break;
								}
								prevEntry = nextEntry;
								nextEntry = nextEntry->next;
							}
						}
					}

					invertedPT[j].pid = procTable[i].pid;
					invertedPT[j].virtualPageNumber = pnum;
					invertedPT[j].frameNumber = oldestFrame->number;
					invertedPT[j].next = NULL;

					oldestFrame->pid = invertedPT[j].pid;
					oldestFrame->virtualPageNumber = invertedPT[j].virtualPageNumber;
					oldestFrame = oldestFrame->lruRight;

					fnum = invertedPT[j].frameNumber;
				}
				else
				{
					int k;
					k = (oldestFrame->virtualPageNumber + oldestFrame->pid) % nFrame;

					if (oldestFrame->pid != -1)
					{
						if (oldestFrame->pid == invertedPT[k].pid && oldestFrame->virtualPageNumber == invertedPT[k].virtualPageNumber)
						{
							if (invertedPT[k].next != NULL)
							{
								invertedPT[k].frameNumber = invertedPT[k].next->frameNumber;
								invertedPT[k].pid = invertedPT[k].next->pid;
								invertedPT[k].virtualPageNumber = invertedPT[k].next->virtualPageNumber;
								invertedPT[k].next = invertedPT[k].next->next;
							}
							else
							{
								invertedPT[k].frameNumber = -1;
								invertedPT[k].pid = -1;
								invertedPT[k].virtualPageNumber = -1;
								invertedPT[k].next = NULL;
							}
						}
						else
						{
							nextEntry = &invertedPT[k];
							prevEntry = nextEntry;
							nextEntry = nextEntry->next;
							while (nextEntry != NULL)
							{
								if (oldestFrame->pid == invertedPT[k].pid && oldestFrame->virtualPageNumber == invertedPT[k].virtualPageNumber)
								{
									prevEntry->next = nextEntry->next;
									free(nextEntry);
									break;
								}

								if (nextEntry->next == NULL)
								{
									break;
								}
								prevEntry = nextEntry;
								nextEntry = nextEntry->next;
							}
						}
					}

					nextEntry = &invertedPT[j];
					temp1 = (struct invertedPageTableEntry *)malloc(sizeof(struct invertedPageTableEntry));
					temp1->pid = procTable[i].pid;
					temp1->virtualPageNumber = pnum;
					temp1->frameNumber = oldestFrame->number;
					temp1->next = NULL;
					//lru
					oldestFrame->pid = temp1->pid;
					oldestFrame->virtualPageNumber = temp1->virtualPageNumber;
					oldestFrame = oldestFrame->lruRight;

					//해쉬테이블 엔트리의 연결리스트 맨 앞에 fault났던 페이지 배치
					temp1->next = invertedPT[j].next;
					invertedPT[j].next = temp1;
					struct invertedPageTableEntry temp2;
					temp2.pid = invertedPT[j].pid;
					temp2.virtualPageNumber = invertedPT[j].virtualPageNumber;
					temp2.frameNumber = invertedPT[j].frameNumber;

					invertedPT[j].pid = temp1->pid;
					invertedPT[j].virtualPageNumber = temp1->virtualPageNumber;
					invertedPT[j].frameNumber = temp1->frameNumber;
					temp1->pid = temp2.pid;
					temp1->virtualPageNumber = temp2.virtualPageNumber;
					temp1->frameNumber = temp2.frameNumber;

					fnum = invertedPT[j].frameNumber;
				}
			}
			procTable[i].ntraces++;

			physicalAddr = (unsigned)offset | (unsigned)(fnum << PAGESIZEBITS); //offset과frame number의 or연산 해야
			printf("IHT procID %d traceNumber %d virtual addr %x pysical addr %x\n", i,
				   procTable[i].ntraces, addr, physicalAddr);
		}
		else if (b_numprocess[i] == 0)
		{
			b_cnt++;
			b_numprocess[i] = 1;
		}
		i++;
		if (i >= numProcess)
		{
			i = 0;
		}
	}

	nextEntry = NULL;
	prevEntry = NULL;
	invertedPT = NULL;
	b_numprocess = NULL;
	temp1 = NULL;
	free(nextEntry);
	free(prevEntry);
	free(invertedPT);
	free(b_numprocess);
	free(temp1);

	//시뮬레이션 완료 후, 결과 출력
	for (i = 0; i < numProcess; i++)
	{
		printf("**** %s *****\n", procTable[i].traceName);
		printf("Proc %d Num of traces %d\n", i, procTable[i].ntraces);
		printf("Proc %d Num of Inverted Hash Table Access Conflicts %d\n", i, procTable[i].numIHTConflictAccess);
		printf("Proc %d Num of Empty Inverted Hash Table Access %d\n", i, procTable[i].numIHTNULLAccess);
		printf("Proc %d Num of Non-Empty Inverted Hash Table Access %d\n", i, procTable[i].numIHTNonNULLAcess);
		printf("Proc %d Num of Page Faults %d\n", i, procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n", i, procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
		assert(procTable[i].numIHTNULLAccess + procTable[i].numIHTNonNULLAcess == procTable[i].ntraces);
	}

	fclose(file);
}

int main(int argc, char *argv[])
{
	firstLevelBits = atoi(argv[1]);
	phyMemSizeBits = atoi(argv[2]);
	numProcess = argc - 3;

	int i;
	struct procEntry *procTbl = (struct procEntry *)malloc(sizeof(struct procEntry) * numProcess);

	if (argc < 4)
	{
		printf("Usage : %s firstLevelBits PhysicalMemorySizeBits TraceFileNames\n", argv[0]);
		exit(1);
	}

	if (phyMemSizeBits < PAGESIZEBITS)
	{
		printf("PhysicalMemorySizeBits %d should be larger than PageSizeBits %d\n", phyMemSizeBits, PAGESIZEBITS);
		exit(1);
	}
	if (VIRTUALADDRBITS - PAGESIZEBITS - firstLevelBits <= 0)
	{
		printf("firstLevelBits %d is too Big\n", firstLevelBits);
		exit(1);
	}

	// initialize procTable for two-level page table
	for (i = 0; i < numProcess; i++)
	{
		// opening a tracefile for the process
		printf("process %d opening %s\n", i, argv[i + 3]);
		procTbl[i].traceName = argv[i + 3];
		procTbl[i].pid = i;
		procTbl[i].ntraces = 0;
		procTbl[i].num2ndLevelPageTable = 0;
		procTbl[i].numPageFault = 0;
		procTbl[i].numPageHit = 0;
		procTbl[i].firstLevelPageTable = NULL;
	}

	unsigned int nFrame = (1 << (phyMemSizeBits - PAGESIZEBITS));
	assert(nFrame > 0);

	struct framePage *phyM = (struct framePage *)malloc(sizeof(struct framePage) * nFrame);
	initPhyMem(phyM, nFrame);

	printf("\nNum of Frames %d Physical Memory Size %ld bytes\n", nFrame, (1L << phyMemSizeBits));

	printf("=============================================================\n");
	printf("The 2nd Level Page Table Memory Simulation Starts .....\n");
	printf("=============================================================\n");

	// two-level page table 진행
	secondLevelVMSim(procTbl, phyM);

	// initialize procTable for the inverted Page Table
	for (i = 0; i < numProcess; i++)
	{
		procTbl[i].traceName = argv[i + 3];
		procTbl[i].pid = i;
		procTbl[i].ntraces = 0;
		procTbl[i].numIHTConflictAccess = 0;
		procTbl[i].numIHTNULLAccess = 0;
		procTbl[i].numIHTNonNULLAcess = 0;
		procTbl[i].numPageFault = 0;
		procTbl[i].numPageHit = 0;
	}
	initPhyMem(phyM, nFrame);

	printf("=============================================================\n");
	printf("The Inverted Page Table Memory Simulation Starts .....\n");
	printf("=============================================================\n");

	invertedPageVMSim(procTbl, phyM, nFrame);

	free(phyM);
	free(procTbl);
	return (0);
}