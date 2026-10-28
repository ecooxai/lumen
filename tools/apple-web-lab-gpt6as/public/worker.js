self.onmessage = event => {
  const limit = event.data?.limit;
  if (!Number.isInteger(limit) || limit < 0 || limit > 1000000) return self.postMessage({error:'Invalid bounded work limit'});
  let sum=0; for(let i=1;i<=limit;i++)sum+=i;
  self.postMessage({sum});
};
