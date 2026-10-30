import ast
import json
from pathlib import Path
import unittest

BASE=Path(__file__).parent
def functions(name, names):
    tree=ast.parse((BASE/name).read_text())
    selected=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in names]
    return tree,selected

class NavigationEvidence(unittest.TestCase):
    def test_new_observer_preserves_the_exact_playback_assessment(self):
        old,old_functions=functions('public_video_gesture_diagnostic_case.py',{'assess'})
        new,new_functions=functions('public_video_gesture_ready_case.py',{'assess'})
        self.assertEqual(ast.dump(old_functions[0]),ast.dump(new_functions[0]))
        for tree in (old,new):
            limit=next(ast.literal_eval(node.value) for node in tree.body if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='LIMIT' for t in node.targets))
            self.assertEqual(limit,80)

    def sanitizer(self):
        tree=ast.parse((BASE/'public_video_gesture_ready_case.py').read_text())
        selected=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in {'finite','sanitize_line'} or
                  isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id in {'BOOLS','NUMBERS','ENUMS','EVENTS','REASONS'} for t in node.targets)]
        scope={'json':json,'math':__import__('math')}
        exec(compile(ast.Module(body=selected,type_ignores=[]),'actual-ready-evidence','exec'),scope)
        return scope['sanitize_line']

    def test_readiness_boolean_proof_is_allowlisted_without_identifiers(self):
        marker='LUME_PUBLIC_VIDEO_DIAGNOSTIC '
        source={'schema':1,'phase':'wait-navigation-ready','cdpMethod':'Page.setLifecycleEventsEnabled',
                'navigationSelected':True,'navigationDOMContentLoadedObserved':True,'navigationLoadObserved':False,
                'navigationReceiptCount':1,'navigationReadyWaitMs':300,'frameId':'PRIVATE_FRAME',
                'loaderId':'PRIVATE_LOADER','url':'https://private.invalid/TOKEN','rawError':'PRIVATE_ERROR'}
        kind,row=self.sanitizer()(marker+json.dumps(source))
        self.assertEqual(kind,'diagnostic');self.assertEqual(row['navigationReadyWaitMs'],300)
        self.assertTrue(row['navigationDOMContentLoadedObserved'])
        self.assertNotIn('PRIVATE',json.dumps(row));self.assertNotIn('TOKEN',json.dumps(row))

    def test_untyped_readiness_cannot_be_promoted_to_a_boolean_pass(self):
        _,row=self.sanitizer()('LUME_PUBLIC_VIDEO_DIAGNOSTIC '+json.dumps({'schema':1,
            'navigationDOMContentLoadedObserved':'true','navigationSelected':1,'navigationReadyWaitMs':True,
            'navigationReceiptCount':-1,'phase':'PRIVATE_PHASE'}))
        self.assertNotIn('navigationDOMContentLoadedObserved',row)
        self.assertNotIn('navigationSelected',row)
        self.assertNotIn('navigationReadyWaitMs',row)
        self.assertNotIn('navigationReceiptCount',row)
        self.assertNotIn('phase',row)

if __name__=='__main__':unittest.main()
