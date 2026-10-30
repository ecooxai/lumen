import hashlib,json,pathlib
p=pathlib.Path('src/js/prelude/04-tags.js')
OLD='0594cd440a3ae7b3ccc924935f2bd36ce19504f52454d578854490ca25c95ce1';NEW='b0549b35e1feab47aa977f0aad14907275c6432884ea749b87e19d52ea1ad9bf'
anchor="mk('HTMLPictureElement', HTMLElement, ['picture']); mk('HTMLTimeElement', HTMLElement, ['time'], p => reflectStr(p, 'dateTime'));\n"
addition="// Use each tag's HTML interface for both newly created nodes and native clones.\n// The shared wrapper path deliberately reconstructs this prototype instead of\n// copying a prototype that page script may have installed on the source node.\nmk('HTMLTableCaptionElement', HTMLElement, ['caption']);\nmk('HTMLTableColElement', HTMLElement, ['col', 'colgroup']);\nmk('HTMLDataElement', HTMLElement, ['data']);\nmk('HTMLDataListElement', HTMLElement, ['datalist']);\nmk('HTMLModElement', HTMLElement, ['del', 'ins']);\nmk('HTMLDirectoryElement', HTMLElement, ['dir']);\nmk('HTMLFontElement', HTMLElement, ['font']);\nmk('HTMLFrameElement', HTMLElement, ['frame']);\nmk('HTMLFrameSetElement', HTMLElement, ['frameset']);\nmk('HTMLLegendElement', HTMLElement, ['legend']);\nmk('HTMLMapElement', HTMLElement, ['map']);\nmk('HTMLMeterElement', HTMLElement, ['meter']);\nmk('HTMLOptGroupElement', HTMLElement, ['optgroup']);\nmk('HTMLOutputElement', HTMLElement, ['output']);\nmk('HTMLParamElement', HTMLElement, ['param']);\nmk('HTMLProgressElement', HTMLElement, ['progress']);\n"
def sha():return hashlib.sha256(p.read_bytes()).hexdigest()
before=sha()
if before not in(OLD,NEW):raise RuntimeError('HTML tag registry changed after reviewed constructor RED')
if before==OLD:
 source=p.read_text()
 if source.count(anchor)!=1:raise RuntimeError('Tag registry anchor not unique')
 p.write_text(source.replace(anchor,anchor+addition))
if sha()!=NEW:raise RuntimeError('HTML constructor candidate source mismatch')
print(json.dumps({'source':str(p),'beforeSHA256':OLD,'afterSHA256':NEW,'registeredInterfaces':16,'mappedTags':18,'scope':'Canonical HTML interface prototypes and native clone branding; not complete interface IDL behavior'}))
