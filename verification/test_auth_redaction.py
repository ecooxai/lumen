import json
import unittest
from auth_case import sanitize, BOOL_FIELDS

class PrivacyBoundary(unittest.TestCase):
    def valid(self):
        return dict(schema=1,site='google',elapsedMs=5000,readyState='complete',providerLoggedInFlag=None,**{key:False for key in BOOL_FIELDS})
    def test_only_allowlisted_typed_values_survive(self):
        item=self.valid();item.update(email='private@example.invalid',cookie='secret',url='https://example.invalid/?token=secret')
        cleaned=sanitize('LUME_AUTH_STATUS '+json.dumps(item))
        self.assertIsNotNone(cleaned)
        self.assertNotIn('secret',json.dumps(cleaned));self.assertNotIn('email',cleaned);self.assertNotIn('url',cleaned)
    def test_secret_in_allowed_field_rejected(self):
        item=self.valid();item['uiAccountIndicator']='private@example.invalid'
        self.assertIsNone(sanitize('LUME_AUTH_STATUS '+json.dumps(item)))
    def test_unknown_site_and_state_rejected(self):
        for field in ('site','readyState'):
            item=self.valid();item[field]='private@example.invalid'
            self.assertIsNone(sanitize('LUME_AUTH_STATUS '+json.dumps(item)))
    def test_raw_logs_and_oversize_rejected(self):
        self.assertIsNone(sanitize('https://example.invalid?token=secret'))
        self.assertIsNone(sanitize('LUME_AUTH_STATUS '+'x'*5000))
    def test_known_native_console_prefix(self):
        self.assertIsNotNone(sanitize('[js log] LUME_AUTH_STATUS '+json.dumps(self.valid())))
    def test_geometry_is_bounded_numeric_only(self):
        item=self.valid();item.update(viewportWidth=1024,viewportHeight=724,devicePixelRatio=2)
        self.assertEqual(sanitize('LUME_AUTH_STATUS '+json.dumps(item))['devicePixelRatio'],2)
        for field,bad in [('viewportWidth','private@example.invalid'),('viewportHeight',True),('devicePixelRatio',float('nan'))]:
            value=item.copy();value[field]=bad
            self.assertIsNone(sanitize('LUME_AUTH_STATUS '+json.dumps(value)))
    def test_numeric_bool_rejected(self):
        item=self.valid();item['composerPresent']=1
        self.assertIsNone(sanitize('LUME_AUTH_STATUS '+json.dumps(item)))

if __name__=='__main__':unittest.main()
