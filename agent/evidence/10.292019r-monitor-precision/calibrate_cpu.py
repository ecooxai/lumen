import time
started=time.monotonic()
for second in range(30):
    cpu=time.process_time()
    while time.process_time()-cpu < .1:
        pass
    time.sleep(max(0,started+second+1-time.monotonic()))
