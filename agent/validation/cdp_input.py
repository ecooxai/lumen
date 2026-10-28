"""Test-fixture native event bridge; no DOM text injection."""
def dispatch(call,session,ops):
 mods=0;x=y=0
 special={57353:('Control','ControlLeft',17,2),57352:('Shift','ShiftLeft',16,8),57354:('Alt','AltLeft',18,1),57351:('Enter','Enter',13,0),57347:('Backspace','Backspace',8,0),57348:('Tab','Tab',9,0),57350:('Enter','Enter',13,0),57356:('Escape','Escape',27,0),57360:('End','End',35,0),57361:('Home','Home',36,0),57362:('ArrowLeft','ArrowLeft',37,0),57363:('ArrowUp','ArrowUp',38,0),57364:('ArrowRight','ArrowRight',39,0),57365:('ArrowDown','ArrowDown',40,0),57367:('Delete','Delete',46,0)}
 for line in ops.splitlines():
  parts=line.split();op=parts[0]
  if op=='move':
   x,y=map(float,parts[1:3]);call('Input.dispatchMouseEvent',{'type':'mouseMoved','x':x,'y':y},session)
  elif op in ('down','up'):
   call('Input.dispatchMouseEvent',{'type':'mousePressed' if op=='down' else 'mouseReleased','x':x,'y':y,'button':'left','buttons':1 if op=='down' else 0,'clickCount':1},session)
  elif op in ('key','keyup'):
   cp=int(parts[1]);up=op=='keyup'
   if cp in special:
    key,code,vk,mask=special[cp];mods=mods&~mask if up else mods|mask
    call('Input.dispatchKeyEvent',{'type':'keyUp' if up else 'rawKeyDown','key':key,'code':code,'windowsVirtualKeyCode':vk,'modifiers':mods},session)
   else:
    ch=chr(cp);vk=(ord(ch.upper()) if ch.isalpha() else cp) if cp<128 else 0;code=('Key'+ch.upper()) if cp<128 and ch.isalpha() else ('Space' if ch==' ' else '')
    if up:call('Input.dispatchKeyEvent',{'type':'keyUp','key':ch,'code':code,'windowsVirtualKeyCode':vk,'modifiers':mods},session)
    elif mods:
     call('Input.dispatchKeyEvent',{'type':'rawKeyDown','key':ch,'code':code,'windowsVirtualKeyCode':vk,'modifiers':mods},session);call('Input.dispatchKeyEvent',{'type':'keyUp','key':ch,'code':code,'windowsVirtualKeyCode':vk,'modifiers':mods},session)
    else:
     call('Input.dispatchKeyEvent',{'type':'keyDown','key':ch,'code':code,'windowsVirtualKeyCode':vk,'text':ch,'unmodifiedText':ch},session);call('Input.dispatchKeyEvent',{'type':'keyUp','key':ch,'code':code,'windowsVirtualKeyCode':vk},session)
