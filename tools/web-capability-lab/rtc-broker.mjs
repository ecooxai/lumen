const ROLE = new Set(['offer','answer']);
const ROOM = /^[a-zA-Z0-9_-]{8,80}$/;

export function createRTCBroker({clock=Date.now,ttl=120000,maxRooms=32}={}) {
  const rooms=new Map();
  function cleanup(){for(const [id,room] of rooms)if(clock()-room.created>=ttl)rooms.delete(id);}
  function access(id,role){
    cleanup();
    if(!ROOM.test(id||'')||!ROLE.has(role))return {error:400,detail:'Invalid room or role'};
    if(!rooms.has(id)){if(rooms.size>=maxRooms)return {error:429,detail:'Room limit reached'};rooms.set(id,{created:clock(),sequence:0,offer:[],answer:[],seen:new Set()});}
    const room=rooms.get(id);room.seen.add(role);return room;
  }
  return {
    poll(id,role,after=0){const room=access(id,role);if(room.error)return room;return {messages:room[role].filter(item=>item.sequence>after),peerSeen:room.seen.has(role==='offer'?'answer':'offer'),expiresInMs:Math.max(0,ttl-(clock()-room.created))};},
    post(id,role,message){
      const room=access(id,role);if(room.error)return room;
      if(!message||!['offer','answer','complete','error'].includes(message.type))return {error:400,detail:'Invalid signal type'};
      if(['offer','answer'].includes(message.type)&&(message.type!==role||typeof message.sdp!=='string'||message.sdp.length>32768))return {error:400,detail:'Invalid SDP signal'};
      const queue=room[role==='offer'?'answer':'offer'];if(queue.length>=8)return {error:429,detail:'Signal limit reached'};
      const safe=['offer','answer'].includes(message.type)?{type:message.type,sdp:message.sdp}:{type:message.type};
      queue.push({sequence:++room.sequence,message:safe});return {accepted:true,sequence:room.sequence};
    },
    get size(){cleanup();return rooms.size;}
  };
}
