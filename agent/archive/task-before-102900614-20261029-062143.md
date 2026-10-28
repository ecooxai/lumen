 improve lume  browser for apple device . 10290244
pass web platform test like chrome,focus on important test first.
 also run ipa in mac we are now use same ipa fr mac ios ipad. kerp monitor cpu and ram usage ensure no abnormal high usage  . prefer use prebuilt v8 js binary.  show current progress wpt passrate first. utilize many subagent to accelate dev. tell me max number subagents you can spawn and use max them with same model and think levl of main agent




 $project\_dir=~/project/lumen
$dev\_tools=mac and  ipad simulator
$goal\_score= 98/100
$work\_env=
use specified mcp plugin @devinmac3d if you are chatgpt, or use local if you are other agent like codex agy
use local pc if this mcp server not works.
$working_timeout=3h
$iteration_times=    1000        and stop when goal done or working time >workingtimeout
$subagent=prefer use as many subagents as you can,same model and high thinking lvl as main agent
 write this whole text into agent/task.md

 
 
# use smarter subagent
this is only for subagent with same smart or even smarter as main agent, so put most tasks to subagent is ok, only do this if main agent can start same smart or smarter model subagent like chatgpt chat mode gpt pro chat with subagent.
use subagents to do all works, you main agent only for project manager, like cto, plan and evaluate , give all tasks to new subagents, do not coding ,debuging, testing in main agent as much as possible, only do thiese task if necessary, even for evaluatte and plan or analyze, you can start subagents for these task too,you only get most imprtant result info. keep main agent context clearn including only most important info

# junior subagent
if subagents is using low cost and faster smaller models then main agent should does main work ,only gve it simple task like testing and building task. claude agent can also give computer use taks to latest hiku(>=hike5.5) since hiku is good at vision and fast. 





# note

##  dev manage
keep project and code clean . 
if continue a project, read AGENTS.md and agent/continue.md first,commit and switch new branch for this task, and update AGENTS.md and  write new agent/continue.md , mv old one into agent/archive/ with date appendix.
monitor cpu ram usage for software dev every 1s when test,keep fast performace and less ram usage, if sonething too slow,high cpu and ram usage find why and deal with it first for fast dev.
use your model name and agent tool and keytool nmes like web or blender name when name finnal output files and new project
use git to manage project,create a new branch with your model name and agent name+branchname ,save to https://fileharbor-meak.basicgleam65.chatgpt.site/ via its rest api     or  chatgpt library after a important progress, alwasy save after each 10 iterations.  

### iterate
iterate user specified number and give score of your work n/100 every iteration . 100 isperfect quality like 3A game production software ,hd graphic , easy use both on pc and phone. same check for each subagent if user need ask subagent. after subagent finish ,main agent should check quality and if not good then start a new subagent to improve.  record each iteration start time and end time, time cost in main and subagents used write to agent/devsummary.md and calculte total times cost from start to last . 
record each test,build task time cost too,if one of them cost more than 5mins find why if abnormal then try to optimize testing or building

do not stop util get high score or time >$workingtimeout or 4h



## context improve
do not run cmd that ouput large base64 string into output like stdout and other place that will show full base64 str, only do convert when output base64 str into a file, if you really need output base64 directly try only get first 50 char and last 50 char to output for verify is enough

## 3d
do not use other skill.md use your own way
when build 3d scene or drawing and other vision task,  try less use code to analize the orignal uploaded image, prefer use your vision ability instead , do not use exsited files outside the project dir donot use assets from ineternet , do not cheat, image 4 sides views  and make sure all sides views looks natural and good. use light blue as default background if bg not specify.
show ieration number and score at bottom right of web dev previewer ui. 
do not genrate image unless i ask you genreate image explicitly.
prefer use web to build 3d obj  not blender(use eevee for fast preview if user ask to use blend explicitly). 
when build web 3d, prefer use webgpu and prompt user fallback to webgl if webgpu not works never fallback autoly(for dev if no gpu/webgpu, you can use solftrender or webgl to preview). user can choose backend and resolution in setting ui. default rsolution should be 900x1200,canvas should same css width height as render resolution fo not fit to window , user can set to 480 1280 1080 2k 4k vertial or landspace. make cava css width to 1/2 if browser window is less than 900 

## env falkback
when dev env not works,wait 20s and  try again for 3 times. then fallback to local or other env. never stop util you get 95/100 score. 


use  https tunnel to let use preview latest work or progress(show and refresh evry 2s system cpu ram and all disk info in GB latency ms from server to client via websocket) and update deploy to surge.sh (include git repo to /src.tar.gz too)after each iteration. check opentunnel is running well after each iteration.  also add input box to send msg to server and main agent should receive the msg and response in time . ( prefer to use a deploy subagent for this task if can use subagent or do it in main agent if subagent not available)

curl -fsSL https://opentunnel.xyz/install | sh
opentunnel route add 8080  or other port  
npm install --global surge
cd webappdir && surge 

when deploy to chatgpt Sites if its 3d demo depoy to demo3d site(create if it not exsited)


# chatgpt notes

these are for chatgpt app If you are not, you can ignore it.
and if the mcp or env error, wait for 30s then try again, wait and retry at least for 20 tim, fallback to localvm if you are chatgpt

the most important is avoid stop while score <95.do not treat anythig as stoppoint only treat 95scre as stopping point.
## subagent
use builtin subagents if you can, not mcpagent only use built in subagent, subagent should use same model and think level,same quaick score check.
for 3d modeling multi objects,use subagent to build each

## never stop util high scoe95  ;avoid ending session and other stop point
when dev env not works,wait 20s and  try again for 3 times. then fallback to local vm. never stop util you get 95/100 score.  for exception when you stop whil score lessthan 95 (try to avoid this in max effort) tell user why stop and try continue agian
 avoid ending working session in any case
you need not ended the working session to report progress instead of continuing development, need not report progress you must  keep working session and  improving util 95score high quality
never incorrectly treated the successful build, testing, and packaging as a stopping point only treat goal score complete as stopping point.   never treated a verified prototype checkpoint or anything else as a stopping point

     
## post task

only do these after get finish goal as finnal stop:
update AGENTS.md remove outdated info
write summary give neceesary conext for next agent start new task in end of AGENTS.md
push git and deploy to cloud like github page or cloudflare . when ouput image video audio or 3d obj, create  showcase version copys files too,put the model and agent tool name below with big white font and black bg to show who created this work clearly.  createa video 4:3ess than   60s to show your works well.

you need not ended the working session to report progress instead of continuing development, need not report progress you must  keep working session and  improving util  high quality. do not pause the working session before achive goal and score for post task

 












 
 $project\_dir=~/project/lumen
$dev\_tools=mac and  ipad simulator
$goal\_score= 98/100
$work\_env=
use specified mcp plugin @devinmac3d if you are chatgpt, or use local if you are other agent like codex agy
use local pc if this mcp server not works.
$working_timeout=3h
$iteration_times=    1000        and stop when goal done or working time >workingtimeout
$subagent=prefer use as many subagents as you can,same model and high thinking lvl as main agent
 write this whole text into agent/task.md
