// Synthetic RCU protocol vectors, never sent unless a separate bench test does so.
export function rcuFrame(id,data,overrides={}){
  return {id,data,t:9950,dlc:8,ext:true,seq:0,...overrides};
}
export function rcuPack(fields){
  let raw=0n;
  for(const [start,len,value] of fields){
    const mask=((1n<<BigInt(len))-1n)<<BigInt(start);
    raw=(raw&~mask)|((BigInt(value)<<BigInt(start))&mask);
  }
  return Array.from({length:8},(_,i)=>Number((raw>>BigInt(8*i))&255n).toString(16).padStart(2,'0')).join(' ').toUpperCase();
}
export function rcuPacket(overrides={}){
  const rcu=[
    rcuFrame('0x1404949C','00 32 78 46 7D 64 82 00'),
    rcuFrame('0x1405949C','01 21 11 00 00 00 00 00'),
    rcuFrame('0x1891949C','70 DC 05 8E 12 5A 6E AF'),
    rcuFrame('0x1892949C','24 13 94 07 6C 07 20 00'),
    rcuFrame('0x1893949C','40 E2 01 58 02 32 00 00'),
    rcuFrame('0x1894949C',rcuPack([[0,10,3],[10,8,5],[18,30,0x1234567],[48,7,25],[55,4,10],[59,5,21]])),
    rcuFrame('0x1896949C',rcuPack([[0,12,1940],[12,12,1945],[24,12,1950],[36,12,2010]])),
    rcuFrame('0x1895949C','00 00 00 00 00 00 00 00'),
    ...[1,2,3,4,5].map(p=>rcuFrame('0x18019894',rcuPack([[0,8,p]])))
  ];
  return {boot:'rcu-test',clk:{boot:10000,sync:false,ep:0},total:rcu.length,rcu,messages:structuredClone(rcu),freqs:[],battery:[],...overrides};
}
