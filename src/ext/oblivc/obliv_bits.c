// TODO I need to fix some int sizes
#include <obliv_bits.h>
#include <stdio.h>      // for protoUseStdio()

// Q: What's with all these casts to and from void* ?
// A: Code generation becomes easier without the need for extraneous casts.
//      Might fix it some day. But user code never sees these anyway.

// Right now, we do not support multiple protocols at the same time
static ProtocolDesc currentProto;

// --------------------------- Transports -----------------------------------
// Convenience functions
static int recv(ProtocolDesc* pd,int s,void* p,size_t n)
  { return pd->trans->recv(pd,s,p,n); }
static int send(ProtocolDesc* pd,int d,void* p,size_t n)
  { return pd->trans->send(pd,d,p,n); }

struct stdioTransport
{ ProtocolTransport cb;
  bool needFlush;
};

// Ignores 'dest' parameter. So you can't send to yourself
static bool* stdioFlushFlag(ProtocolDesc* pd)
  { return &((struct stdioTransport*)pd->trans)->needFlush; }

static int stdioSend(ProtocolDesc* pd,int dest,const void* s,size_t n)
{ *stdioFlushFlag(pd)=true;
  return fwrite(s,1,n,stdout); 
}

static int stdioRecv(ProtocolDesc* pd,int src,void* s,size_t n)
{ 
  bool *p = stdioFlushFlag(pd);
  if(*p) { fflush(stdout); *p=false; }
  return fread(s,1,n,stdin); 
}

static void stdioCleanup(ProtocolDesc* pd) {}

static struct stdioTransport stdioTransport 
  = {{ stdioSend, stdioRecv, stdioCleanup},false};

void protocolUseStdio(ProtocolDesc* pd)
  { pd->trans = &stdioTransport.cb; }

void cleanupProtocol(ProtocolDesc* pd)
  { pd->trans->cleanup(pd); }

void setCurrentParty(ProtocolDesc* pd, int party)
  { pd->thisParty=party; }

void __obliv_c__assignBitKnown(OblivBit* dest, bool value)
  { dest->knownValue = value; dest->known=true; }

void __obliv_c__copyBit(OblivBit* dest,const OblivBit* src)
  { if(dest!=src) *dest=*src; }

bool __obliv_c__bitIsKnown(const OblivBit* bit,bool* v)
{ if(bit->known) *v=bit->knownValue;
  return bit->known;
}

static int tobool(int x) { return x?1:0; }

// TODO all sorts of identical parameter optimizations
// Implementation note: remember that all these pointers may alias each other
void dbgProtoSetBitAnd(ProtocolDesc* pd,
    OblivBit* dest,const OblivBit* a,const OblivBit* b)
{
  dest->knownValue= (a->knownValue&& b->knownValue);
  dest->known = false;
  currentProto.yaoCount++;
}

void dbgProtoSetBitOr(ProtocolDesc* pd,
    OblivBit* dest,const OblivBit* a,const OblivBit* b)
{
  dest->knownValue= (a->knownValue|| b->knownValue);
  dest->known = false;
  currentProto.yaoCount++;
}
void dbgProtoSetBitXor(ProtocolDesc* pd,
    OblivBit* dest,const OblivBit* a,const OblivBit* b)
{
  dest->knownValue= (tobool(a->knownValue) != tobool(b->knownValue));
  dest->known = false;
  currentProto.xorCount++;
}
void dbgProtoSetBitNot(ProtocolDesc* pd,OblivBit* dest,const OblivBit* a)
{
  dest->knownValue= !a->knownValue;
  dest->known = a->known;
}
void dbgProtoFlipBit(ProtocolDesc* pd,OblivBit* dest) 
  { dest->knownValue = !dest->knownValue; }

void __obliv_c__setBitAnd(OblivBit* dest,const OblivBit* a,const OblivBit* b)
{
  if(a->known || b->known)
  { if(!a->known) { const OblivBit* t=a; a=b; b=t; }
    if(a->knownValue) __obliv_c__copyBit(dest,b);
    else __obliv_c__assignBitKnown(dest,false);
  }else currentProto.setBitAnd(&currentProto,dest,a,b);
}
void __obliv_c__setBitOr(OblivBit* dest,const OblivBit* a,const OblivBit* b)
{
  if(a->known || b->known)
  { if(!a->known) { const OblivBit* t=a; a=b; b=t; }
    if(!a->knownValue) __obliv_c__copyBit(dest,b);
    else __obliv_c__assignBitKnown(dest,true);
  }else currentProto.setBitOr(&currentProto,dest,a,b);
}
void __obliv_c__setBitXor(OblivBit* dest,const OblivBit* a,const OblivBit* b)
{
  bool v;
  if(a->known || b->known)
  { if(!a->known) { const OblivBit* t=a; a=b; b=t; }
    v = a->knownValue;
    __obliv_c__copyBit(dest,b);
    if(v) __obliv_c__flipBit(dest);
  }else currentProto.setBitXor(&currentProto,dest,a,b); 
}
void __obliv_c__setBitNot(OblivBit* dest,const OblivBit* a)
  { currentProto.setBitNot(&currentProto,dest,a); }
void __obliv_c__flipBit(OblivBit* dest) 
  { currentProto.flipBit(&currentProto,dest); }

void __obliv_c__feedOblivBool(OblivBit* dest,int party,bool a)
{ 
  int curparty = __obliv_c__currentParty();
  
  dest->known=false;
  if(party==1) { if(curparty==1) dest->knownValue=a; }
  else if(party==2 && curparty == 1) 
    recv(&currentProto,1,&dest->knownValue,sizeof(bool));
  else if(party==2 && curparty == 2) send(&currentProto,1,&a,sizeof(bool));
  else fprintf(stderr,"Error: This is a 2 party protocol\n");
}
void __obliv_c__feedOblivBits(OblivBit* dest, int party
                             ,const bool* src,size_t size)
  { while(size--) __obliv_c__feedOblivBool(dest++,party,*(src++)); }

inline void __obliv_c__setupOblivBits(OblivInputs* spec,OblivBit*  dest
                                     ,widest_t v,size_t size)
{ spec->dest=dest;
  spec->src=v;
  spec->size=size;
}
inline void dbgProtoFeedOblivInputs(ProtocolDesc* pd,
    OblivInputs* spec,size_t count,int party)
{ while(count--)
  { int i;
    widest_t v = spec->src;
    for(i=0;i<spec->size;++i) 
    { __obliv_c__feedOblivBool(spec->dest+i,party,v&1);
      v>>=1;
    }
    spec++;
  }
}

inline bool __obliv_c__revealOblivBool(const OblivBit* dest,int party)
{ if(party!=0 && party!=currentProto.thisParty) return false;
  else return dest->knownValue;
}
inline widest_t dbgProtoRevealOblivBits
  (ProtocolDesc* pd,const OblivBit* dest,size_t size,int party)
{ widest_t rv=0;
  if(currentProto.thisParty==1)
  { dest+=size;
    while(size-->0) rv = (rv<<1)+tobool((--dest)->knownValue);
    if(party==0 || party==2) send(pd,2,&rv,sizeof(rv));
    if(party==2) return 0;
    else return rv;
  }else // assuming thisParty==2
  { if(party==0 || party==2) { recv(pd,1,&rv,sizeof(rv)); return rv; }
    else return 0;
  }
}

static void broadcastBits(int source,void* p,size_t n)
{
  int i;
  if(currentProto.thisParty!=source) recv(&currentProto,source,p,n);
  else for(i=1;i<=currentProto.partyCount;++i) if(i!=source)
      send(&currentProto,i,p,n);
}

void execDebugProtocol(ProtocolDesc* pd, protocol_run start, void* arg)
{
  pd->feedOblivInputs = dbgProtoFeedOblivInputs;
  pd->revealOblivBits = dbgProtoRevealOblivBits;
  pd->setBitAnd = dbgProtoSetBitAnd;
  pd->setBitOr  = dbgProtoSetBitOr;
  pd->setBitXor = dbgProtoSetBitXor;
  pd->setBitNot = dbgProtoSetBitNot;
  pd->flipBit   = dbgProtoFlipBit;
  pd->partyCount= 2;
  currentProto = *pd;
  currentProto.yaoCount = currentProto.xorCount = 0;
  start(arg);
}

inline widest_t __obliv_c__revealOblivBits(const OblivBit* dest, size_t size,
    int party)
  { return dbgProtoRevealOblivBits(&currentProto,dest,size,party); }

int __obliv_c__currentParty() { return currentProto.thisParty; }

void __obliv_c__setSignedKnown
  (void* vdest, size_t size, long long signed value)
{
  OblivBit* dest=vdest;
  while(size-->0)
  { __obliv_c__assignBitKnown(dest,value&1);
    value>>=1; dest++;
  }
}
void __obliv_c__setUnsignedKnown
  (void* vdest, size_t size, long long unsigned value)
{
  OblivBit* dest=vdest;
  while(size-->0)
  { __obliv_c__assignBitKnown(dest,value&1);
    value>>=1; dest++;
  }
}
void __obliv_c__setBitsKnown(OblivBit* dest, const bool* value, size_t size)
  { while(size-->0) __obliv_c__assignBitKnown(dest++,value++); }
void __obliv_c__copyBits(OblivBit* dest, const OblivBit* src, size_t size)
  { if(dest!=src) while(size-->0) __obliv_c__copyBit(dest++,src++); }
bool __obliv_c__allBitsKnown(const OblivBit* bits, bool* dest, size_t size)
{ while(size-->0) if(!__obliv_c__bitIsKnown(bits++,dest++)) return false;
  return true;
}

void bitwiseOp(OblivBit* dest,const OblivBit* a,const OblivBit* b,size_t size
              ,void (*f)(OblivBit*,const OblivBit*,const OblivBit*))
  { while(size-->0) f(dest++,a++,b++); }


void __obliv_c__setBitwiseAnd (void* vdest
                              ,const void* vop1,const void* vop2
                              ,size_t size)
  { bitwiseOp(vdest,vop1,vop2,size,__obliv_c__setBitAnd); }

void __obliv_c__setBitwiseOr  (void* vdest
                              ,const void* vop1,const void* vop2
                              ,size_t size)
  { bitwiseOp(vdest,vop1,vop2,size,__obliv_c__setBitOr); }

void __obliv_c__setBitwiseXor (void* vdest
                              ,const void* vop1,const void* vop2
                              ,size_t size)
  { bitwiseOp(vdest,vop1,vop2,size,__obliv_c__setBitXor); }

void __obliv_c__setBitwiseNot (void* vdest,const void* vop,size_t size)
{ OblivBit *dest = vdest;
  const OblivBit *op = vop;
  while(size-->0) __obliv_c__setBitNot(dest++,op++); 
}

void __obliv_c__setBitwiseNotInPlace (void* vdest,size_t size)
{ OblivBit *dest=vdest; 
  while(size-->0) __obliv_c__flipBit(dest++); 
}

// carryIn and/or carryOut can be NULL, in which case they are ignored
void __obliv_c__setBitsAdd (void* vdest,void* carryOut
                           ,const void* vop1,const void* vop2
                           ,const void* carryIn
                           ,size_t size)
{
  OblivBit carry,bxc,axc,t;
  OblivBit *dest=vdest;
  const OblivBit *op1=vop1, *op2=vop2;
  size_t skipLast;
  if(size==0)
  { if(carryIn && carryOut) __obliv_c__copyBit(carryOut,carryIn);
    return;
  }
  if(carryIn) __obliv_c__copyBit(&carry,carryIn);
  else __obliv_c__assignBitKnown(&carry,0);
  // skip AND on last bit if carryOut==NULL
  skipLast = (carryOut==NULL);
  while(size-->skipLast)
  { __obliv_c__setBitXor(&axc,op1,&carry);
    __obliv_c__setBitXor(&bxc,op2,&carry);
    __obliv_c__setBitXor(dest,op1,&bxc);
    __obliv_c__setBitAnd(&t,&axc,&bxc);
    __obliv_c__setBitXor(&carry,&carry,&t);
    ++dest; ++op1; ++op2;
  }
  if(carryOut) __obliv_c__copyBit(carryOut,&carry);
  else
  { __obliv_c__setBitXor(&axc,op1,&carry);
    __obliv_c__setBitXor(dest,&axc,op2);
  }
}

void __obliv_c__setPlainAdd (void* vdest
                            ,const void* vop1 ,const void* vop2
                            ,size_t size)
  { __obliv_c__setBitsAdd (vdest,NULL,vop1,vop2,NULL,size); }

void __obliv_c__setBitsSub (void* vdest, void* borrowOut
                           ,const void* vop1,const void* vop2
                           ,const void* borrowIn,size_t size)
{
  OblivBit borrow,bxc,bxa,t;
  OblivBit *dest=vdest;
  const OblivBit *op1=vop1, *op2=vop2;
  size_t skipLast;
  if(size==0)
  { if(borrowIn && borrowOut) __obliv_c__copyBit(borrowOut,borrowIn);
    return;
  }
  if(borrowIn) __obliv_c__copyBit(&borrow,borrowIn);
  else __obliv_c__assignBitKnown(&borrow,0);
  // skip AND on last bit if borrowOut==NULL
  skipLast = (borrowOut==NULL);
  while(size-->skipLast)
  { // c = borrow; a = op1; b=op2; borrow = (b+c)(b+a)+c
    __obliv_c__setBitXor(&bxa,op1,op2);
    __obliv_c__setBitXor(&bxc,&borrow,op2);
    __obliv_c__setBitXor(dest,&bxa,&borrow);
    __obliv_c__setBitAnd(&t,&bxa,&bxc);
    __obliv_c__setBitXor(&borrow,&borrow,&t);
    ++dest; ++op1; ++op2;
  }
  if(borrowOut) __obliv_c__copyBit(borrowOut,&borrow);
  else
  { __obliv_c__setBitXor(&bxa,op1,op2);
    __obliv_c__setBitXor(dest,&bxa,&borrow);
  }
}

void __obliv_c__setPlainSub (void* vdest
                            ,const void* vop1 ,const void* vop2
                            ,size_t size)
  { __obliv_c__setBitsSub (vdest,NULL,vop1,vop2,NULL,size); }

void __obliv_c__setSignExtend (void* vdest, size_t dsize
                              ,const void* vsrc, size_t ssize)
{
  if(ssize>dsize) ssize=dsize;
  OblivBit *dest = vdest;
  __obliv_c__copyBits(vdest,vsrc,ssize);
  const OblivBit* s = ((const OblivBit*)vsrc)+ssize-1;
  dsize-=ssize;
  dest+=ssize;
  while(dsize-->0) __obliv_c__copyBit(dest++,s);
}
void __obliv_c__setZeroExtend (void* vdest, size_t dsize
                              ,const void* vsrc, size_t ssize)
{
  if(ssize>dsize) ssize=dsize;
  OblivBit *dest = vdest;
  __obliv_c__copyBits(vdest,vsrc,ssize);
  dsize-=ssize;
  dest+=ssize;
  while(dsize-->0) __obliv_c__assignBitKnown(dest++,0);
}
void __obliv_c__ifThenElse (void* vdest, const void* vtsrc
                           ,const void* vfsrc, size_t size
                           ,const void* vcond)
{
  // copying out vcond because it could be aliased by vdest
  OblivBit x,a,c=*(const OblivBit*)vcond;
  OblivBit *dest=vdest;
  const OblivBit *tsrc=vtsrc, *fsrc=vfsrc;
  while(size-->0)
  { __obliv_c__setBitXor(&x,tsrc,fsrc);
    __obliv_c__setBitAnd(&a,&c,&x);
    __obliv_c__setBitXor(dest,&a,fsrc);
    ++dest; ++fsrc; ++tsrc;
  }
}

// ltOut and ltIn may alias here
void __obliv_c__setLessThanUnit (OblivBit* ltOut
                                ,const OblivBit* op1, const OblivBit* op2
                                ,size_t size, const OblivBit* ltIn)
{
  // (a+b)(a+1)b + (a+b+1)c = b(a+1)+(a+b+1)c = ab+b+c+ac+bc = (a+b)(b+c)+c
  OblivBit t,x;
  __obliv_c__copyBit(ltOut,ltIn);
  while(size-->0)
  { __obliv_c__setBitXor(&x,op1,op2);
    __obliv_c__setBitXor(&t,op2,ltOut);
    __obliv_c__setBitAnd(&t,&t,&x);
    __obliv_c__setBitXor(ltOut,&t,ltOut);
    op1++; op2++;
  }
}

// assumes size >= 1
void __obliv_c__setLessThanSigned (void* vdest
                                  ,const void* vop1,const void* vop2
                                  ,size_t size)
{
  OblivBit *dest=vdest;
  const OblivBit *op1 = vop1, *op2 = vop2;
  __obliv_c__assignBitKnown(dest,0);
  __obliv_c__setLessThanUnit(dest,op1,op2,size-1,dest);
  __obliv_c__setLessThanUnit(dest,op2+size-1,op1+size-1,1,dest);
}

void __obliv_c__setLessThanOrEqualSigned (void* vdest
                                         ,const void* vop1, const void* vop2
                                         ,size_t size)
{
  __obliv_c__setLessThanSigned(vdest,vop2,vop1,size);
  __obliv_c__flipBit(vdest);
}

void __obliv_c__setLessThanUnsigned (void* vdest
                                    ,const void* vop1,const void* vop2
                                    ,size_t size)
{
  OblivBit *dest=vdest;
  const OblivBit *op1 = vop1, *op2 = vop2;
  __obliv_c__assignBitKnown(dest,0);
  __obliv_c__setLessThanUnit(dest,op1,op2,size,dest);
}

void __obliv_c__setLessOrEqualUnsigned (void* vdest
                                       ,const void* vop1, const void* vop2
                                       ,size_t size)
{
  __obliv_c__setLessThanUnsigned(vdest,vop2,vop1,size);
  __obliv_c__flipBit(vdest);
}

void __obliv_c__setEqualTo (void* vdest
                           ,const void* vop1,const void* vop2
                           ,size_t size)
{
  OblivBit *dest=vdest;
  const OblivBit *op1 = vop1, *op2 =  vop2;
  __obliv_c__setNotEqual(dest,op1,op2,size);
  __obliv_c__flipBit(dest);
}

void __obliv_c__setNotEqual (void* vdest
                            ,const void* vop1,const void* vop2
                            ,size_t size)
{
  OblivBit t;
  OblivBit *dest=vdest;
  const OblivBit *op1=vop1, *op2=vop2;
  __obliv_c__assignBitKnown(dest,0);
  while(size-->0)
  { __obliv_c__setBitXor(&t,op1++,op2++);
    __obliv_c__setBitOr(dest,dest,&t);
  }
}

void __obliv_c__condAdd(const void* vc,void* vdest
                       ,const void* vx,size_t size)
{ OblivBit t[size];
  int i;
  for(i=0;i<size;++i) __obliv_c__setBitAnd(t+i,vc,((OblivBit*)vx)+i);
  __obliv_c__setBitsAdd(vdest,NULL,vdest,t,NULL,size);
}
void __obliv_c__condSub(const void* vc,void* vdest
                       ,const void* vx,size_t size)
{ OblivBit t[size];
  int i;
  for(i=0;i<size;++i) __obliv_c__setBitAnd(t+i,vc,((OblivBit*)vx)+i);
  __obliv_c__setBitsSub(vdest,NULL,vdest,t,NULL,size);
}

// ---- Translated versions of obliv.oh functions ----------------------

// TODO remove __obliv_c prefix and make these functions static/internal
void setupOblivBool(OblivInputs* spec, OblivBit* dest, bool v)
  { __obliv_c__setupOblivBits(spec,dest,v,1); }
void setupOblivChar(OblivInputs* spec, OblivBit* dest, char v)
  { __obliv_c__setupOblivBits(spec,dest,v,bitsize(v)); }
void setupOblivInt(OblivInputs* spec, OblivBit* dest, int v)
  { __obliv_c__setupOblivBits(spec,dest,v,bitsize(v)); }
void setupOblivShort(OblivInputs* spec, OblivBit* dest, short v)
  { __obliv_c__setupOblivBits(spec,dest,v,bitsize(v)); }
void setupOblivLong(OblivInputs* spec, OblivBit* dest, long v)
  { __obliv_c__setupOblivBits(spec,dest,v,bitsize(v)); }
void setupOblivLLong(OblivInputs* spec, OblivBit* dest, long long v)
  { __obliv_c__setupOblivBits(spec,dest,v,bitsize(v)); }

void feedOblivInputs(OblivInputs* spec, size_t count, int party)
  { currentProto.feedOblivInputs(&currentProto,spec,count,party); }

// TODO pass const values by ref later
bool revealOblivBool(__obliv_c__bool src,int party)
  { return (bool)__obliv_c__revealOblivBits(src.bits,1,party); }
char revealOblivChar(__obliv_c__char src,int party)
  { return (char)__obliv_c__revealOblivBits(src.bits,bitsize(char),party); }
int revealOblivInt(__obliv_c__int src,int party)
  { return (int)__obliv_c__revealOblivBits(src.bits,bitsize(int),party); }
short revealOblivShort(__obliv_c__short src,int party)
  { return (short)__obliv_c__revealOblivBits(src.bits,bitsize(short),party); }
long revealOblivLong(__obliv_c__long src,int party)
  { return (long)__obliv_c__revealOblivBits(src.bits,bitsize(long),party); }
long long revealOblivLLong(__obliv_c__lLong src,int party)
  { return (long long)__obliv_c__revealOblivBits(src.bits,bitsize(long long)
                                                 ,party); }

// TODO fix data width
bool ocBroadcastBool(int source,bool v)
{
  char t = v;
  broadcastBits(source,&t,1);
  return t;
}
#define broadcastFun(t,tname)           \
  t ocBroadcast##tname(int source, t v)   \
  { broadcastBits(source,&v,sizeof(v)); \
    return v;                           \
  }
broadcastFun(char,Char)
broadcastFun(int,Int)
broadcastFun(short,Short)
broadcastFun(long,Long)
broadcastFun(long long,LLong)
#undef broadcastFun
