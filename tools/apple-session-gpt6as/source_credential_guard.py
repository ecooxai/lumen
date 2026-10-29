"""Reject credential-shaped source, with two independently reviewed fixture blobs."""

import ast
import hashlib
import re


PATTERNS = (
    re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'),
    re.compile(rb'\b(?:ghp_|gho_|github_pat_)[A-Za-z0-9_]{24,}\b'),
    re.compile(rb'\bsk-proj-[A-Za-z0-9_-]{32,}\b'),
    re.compile(rb'\bAKIA[0-9A-Z]{16}\b'),
)
# These exact historical files contain a single bytes constant used to reject
# private keys in a public CA bundle.  They contain no encoded key or end marker.
# A changed file, another path, or another match receives no exception.
REVIEWED_FIXTURE_BLOBS = {
    'f8a8bf0358c793983b376d8cb1205d7cefc0a6e9':
        'dcbc05f92accd63356e1fcc57c95cc6319021b8cee32384c47f717b86fceb420',
    'dbceff2c1b8b763e2a4adec088a51583d5c42fb4':
        '817837403468d72d5b438b92885f5f6b2cd284d57c66828a9cd7ca00373663b1',
}
FIXTURE_PATH = 'tests/test_mkdevapp.py'
FIXTURE_LITERAL = b'-----BEGIN ' + b'PRIVATE KEY-----'
FIXTURE_LITERAL_SHA256 = '3021d90eb9437b2d8f30e8363695c4418b5e5f1870801b5c317e9398ee0f572d'
REVIEW_PATH = 'agent/evidence/10.30-0110-checkpoint-marker-review/review.json'


def inspect_blob(data, oid, path, private_values=()):
    """Return exact reviewed matches; raise without displaying secret contents."""
    exception = None
    if (path == FIXTURE_PATH
            and REVIEWED_FIXTURE_BLOBS.get(oid) == hashlib.sha256(data).hexdigest()):
        constants = [node.value for node in ast.walk(ast.parse(data))
                     if isinstance(node, ast.Constant) and isinstance(node.value, bytes)
                     and node.value == FIXTURE_LITERAL]
        matches = list(PATTERNS[0].finditer(data))
        if (len(constants) == 1 and len(matches) == 1
                and matches[0].group() == FIXTURE_LITERAL
                and len(FIXTURE_LITERAL) == 27
                and hashlib.sha256(FIXTURE_LITERAL).hexdigest() == FIXTURE_LITERAL_SHA256):
            exception = {
                'gitBlobOID': oid, 'path': path,
                'blobSHA256': REVIEWED_FIXTURE_BLOBS[oid],
                'literalBytes': 27, 'literalSHA256': FIXTURE_LITERAL_SHA256,
                'reason': 'Standalone negative-test bytes literal; no key body or end marker',
                'independentReview': REVIEW_PATH,
            }
    for index, pattern in enumerate(PATTERNS):
        for match in pattern.finditer(data):
            if index == 0 and exception is not None and match.group() == FIXTURE_LITERAL:
                continue
            raise RuntimeError('Credential-shaped content in reachable source history; publication withheld: ' + path)
    if any(value in data for value in private_values):
        raise RuntimeError('Private message key in reachable source history; publication withheld: ' + path)
    return [exception] if exception is not None else []
