import test from 'node:test';
import assert from 'node:assert/strict';
import {createRTCBroker} from '../rtc-broker.mjs';

test('RTC signals reach only the opposite role and sequence polling is idempotent',()=>{
  const broker=createRTCBroker();broker.post('test-room-1','offer',{type:'offer',sdp:'v=0\r\n'});
  assert.equal(broker.poll('test-room-1','offer').messages.length,0);
  const delivered=broker.poll('test-room-1','answer');assert.equal(delivered.messages.length,1);assert.equal(delivered.messages[0].message.type,'offer');
  assert.equal(broker.poll('test-room-1','answer',delivered.messages[0].sequence).messages.length,0);
});
test('RTC room data expires without a background timer',()=>{
  let now=0;const broker=createRTCBroker({clock:()=>now,ttl:100});broker.post('test-room-2','offer',{type:'offer',sdp:'v=0'});now=101;assert.equal(broker.size,0);assert.equal(broker.poll('test-room-2','answer').messages.length,0);
});
test('RTC broker limits rooms, payload and role misuse',()=>{
  const broker=createRTCBroker({maxRooms:1});assert.equal(broker.post('test-room-3','offer',{type:'answer',sdp:'v=0'}).error,400);assert.equal(broker.post('test-room-3','offer',{type:'offer',sdp:'x'.repeat(32769)}).error,400);assert.equal(broker.poll('test-room-4','answer').error,429);assert.equal(broker.poll('../private','offer').error,400);
});
