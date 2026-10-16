#!/usr/bin/env python3
"""Publish narrowly sanitized failure context from THIS authorized Actions run.
No credential output; no authorization on signed storage redirects; no git writes.
"""
import json,os,re
from urllib.request import Request,build_opener,HTTPRedirectHandler,urlopen
from urllib.error import HTTPError
from urllib.parse import urlsplit
REPO='murongqiaochu-hub/zmk-config-9981-pro'
assert os.environ.get('GITHUB_REPOSITORY')==REPO
run=os.environ['GITHUB_RUN_ID'];assert run.isdigit()
base='https://api.github.com/repos/'+REPO
headers={'User-Agent':'P9981-scoped-failure-context','Authorization':'Bearer '+os.environ['CI_LOG_TOKEN']}
def get(path):
 with urlopen(Request(base+path,headers=headers),timeout=30) as f:return json.load(f)
record=get('/actions/runs/'+run);assert record['head_sha']==os.environ['GITHUB_SHA']
jobs=get('/actions/runs/'+run+'/jobs?per_page=100')['jobs']
failed=[j for j in jobs if j['conclusion']=='failure' and j['name']!='failure_context']
assert failed,'No failed build/regression job in this run'
class NoRedirect(HTTPRedirectHandler):
 def redirect_request(self,*args,**kwargs):return None
for job in failed[:2]:
 try:
  with build_opener(NoRedirect).open(Request(base+'/actions/jobs/'+str(job['id'])+'/logs',headers=headers),timeout=30) as f:data=f.read()
 except HTTPError as e:
  if e.code not in (301,302,303,307,308):raise
  location=e.headers['Location'];assert urlsplit(location).scheme=='https'
  with urlopen(Request(location,headers={'User-Agent':'P9981-scoped-failure-context'}),timeout=30) as f:data=f.read()
 lines=[]
 for line in data.decode('utf-8','replace').splitlines():
  line=re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]','',line)
  line=re.sub(r'^\d{4}-\d\d-\d\dT\S+\s*','',line)
  if re.search(r'\b(authorization|password|token)\b',line,re.I):continue
  if line.startswith('ccache ') or (len(line)>800 and ' -I' in line and ' -c ' in line):continue
  line=re.sub(r'(?:gh[pousr]_[A-Za-z0-9_]+|github_pat_[A-Za-z0-9_]+)','[REDACTED]',line)
  lines.append(line)
 indices=[i for i,l in enumerate(lines) if re.search(r'CMake Error|error:|undefined reference|FAILED|FATAL ERROR|Traceback|AssertionError|FAIL:',l,re.I)]
 keep=set()
 for i in indices:keep.update(range(max(0,i-2),min(len(lines),i+12)))
 text='\n'.join(lines[i] for i in sorted(keep))[-6500:] if keep else 'No compiler/test pattern found; full logs remain available to authorized viewers.'
 text='Failed job '+str(job['id'])+' '+job['name']+'\n'+text
 with open(os.environ['GITHUB_STEP_SUMMARY'],'a',encoding='utf-8') as f:f.write('### Sanitized current failure\n\n```\n'+text+'\n```\n')
 print('::notice title=ARM failure context::'+text.replace('%','%25').replace('\r','%0D').replace('\n','%0A'))
