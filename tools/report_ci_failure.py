#!/usr/bin/env python3
"""Expose only sanitized compiler context as a check annotation, never credentials.
Runs inside the same repository's Actions with its normal actions-read token.
"""
import json,os,re,sys
from urllib.request import Request,build_opener,HTTPRedirectHandler,urlopen
from urllib.error import HTTPError
from urllib.parse import urlsplit
assert os.environ.get('GITHUB_REPOSITORY')=='murongqiaochu-hub/zmk-config-9981-pro'
assert len(sys.argv)==2 and sys.argv[1].isdigit()
base='https://api.github.com/repos/murongqiaochu-hub/zmk-config-9981-pro'
headers={'User-Agent':'P9981-compiler-diagnostic','Authorization':'Bearer '+os.environ['CI_LOG_TOKEN']}
with urlopen(Request(base+'/actions/jobs/'+sys.argv[1],headers=headers),timeout=30) as f:job=json.load(f)
assert job['conclusion']=='failure'
class NoRedirect(HTTPRedirectHandler):
 def redirect_request(self,*args,**kwargs):return None
try:
 with build_opener(NoRedirect).open(Request(base+'/actions/jobs/'+sys.argv[1]+'/logs',headers=headers),timeout=30) as f:data=f.read()
except HTTPError as e:
 if e.code not in (301,302,303,307,308):raise
 location=e.headers['Location'];assert urlsplit(location).scheme=='https'
 # Authorization MUST NOT follow GitHub's signed log-storage redirect.
 with urlopen(Request(location,headers={'User-Agent':'P9981-compiler-diagnostic'}),timeout=30) as f:data=f.read()
lines=[]
for line in data.decode('utf-8','replace').splitlines():
 line=re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]','',line)
 line=re.sub(r'^\d{4}-\d\d-\d\dT\S+\s*','',line)
 if re.search(r'\b(authorization|password|token)\b',line,re.I):continue
 line=re.sub(r'(?:gh[pousr]_[A-Za-z0-9_]+|github_pat_[A-Za-z0-9_]+)','[REDACTED]',line)
 lines.append(line)
indices=[i for i,l in enumerate(lines) if re.search(r'CMake Error|error:|undefined reference|FAILED:|FATAL ERROR',l,re.I)]
keep=set()
for i in indices:
 keep.update(range(max(0,i-2),min(len(lines),i+14)))
text='\n'.join(lines[i] for i in sorted(keep))[-6500:] if keep else 'No compiler error pattern found; logs remain private to authorized viewers.'
text='Previous failed job '+sys.argv[1]+'\n'+text
with open(os.environ['GITHUB_STEP_SUMMARY'],'a',encoding='utf-8') as f:f.write('### Sanitized previous compiler context\n\n```\n'+text+'\n```\n')
escaped=text.replace('%','%25').replace('\r','%0D').replace('\n','%0A')
print('::notice title=Previous ARM compiler context::'+escaped)
