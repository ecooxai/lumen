"""Independent evidence guard for the actual browser transport probe."""
REQUIRED=('payloadMatched','datagramEchoMatched','reliableStreamEchoMatched','streamFinished','closedPromiseResolved')
ZERO_KEYS=('activeScopes','registeredCleanups','activeMediaTracks')
def assess(summary,contract,events,start,end,source_unchanged):
    test=summary.get('lab_results',{}).get('webtransport',{});data=test.get('evidence',{});cap=contract.get('capabilities') or {}
    counts={kind:sum(row.get('echoBytes',0) for row in events if row.get('event')==kind) for kind in ('datagram','stream')}
    valid_events=bool(events) and all(row.get('transport')=='h3-quic' and start<=row.get('time',-1)<=end and isinstance(row.get('echoBytes'),int) and row['echoBytes']>=0 for row in events)
    clean=all(summary.get('post_stop_evidence',{}).get('resources',{}).get(key)==0 for key in ZERO_KEYS)
    backend=cap.get('webTransport') is True and cap.get('webTransportBackend')=='wtransport-0.7.2' and cap.get('webTransportBYOB') is False and cap.get('webTransportGracefulRemoteClose') is False and contract.get('capabilitiesFrozen') is True and contract.get('propertyReadOnly') is True and contract.get('constructorPresent') is True
    matched=valid_events and counts['datagram']==data.get('bytes') and counts['stream']==data.get('streamBytes') and counts['datagram']>0 and counts['stream']>0
    passed=summary.get('workload_complete') is True and summary.get('observed_browser_samples',0)>=10 and test.get('state')=='pass' and all(data.get(key) is True for key in REQUIRED) and data.get('quicVerified') is True and clean and backend and matched and source_unchanged
    return {'passed':passed,'requiredFlags':list(REQUIRED),'backendDeclarationVerified':backend,'cleanupVerified':clean,'serverByteSums':counts,'independentServerPayloadsCorrelated':matched,'sourcesUnchangedDuringRun':source_unchanged,'browserResult':test,'contract':contract,'postStopResources':summary.get('post_stop_evidence',{}).get('resources')}
