[$devinmac3d](app://asdk_app_6ad495cdfcb8819180822193312bbd82) \
\
\
/goal  improve broser for apple device\
\
make evrything work in ios ipad  improve performce for google login youtube chatgpt make alll fubctiob works lie safari alsorun ipa in mac we are now use same ipa fr mac ios ipad. kerp monitor cpu and ram usage ensure no abnormal high usage\
\
$iteration_times=\
&#x20;          10000 util get goal done\
$work_env=\
&#x20;        use specified mcp plugin if you are chatgpt, or use local if you are other agent like codex agy,\
&#x20;        use local pc if this mcp server not works\
$project_dir=\
&#x20;     \~/project/lume\
\
$dev_tools=\
&#x20;          mac with ios ipad simulator\
$goal_score= 98/100\
\
\
and if the env error, wait for 30s then try again, wait and retry at least for 20 times if you are chatgot ,then fallback to local pc if you are chatgpt, if not chatgpt, just exit\
\
\
write this whole text into task.md\
\
\
\
\
\# note\
\
\
\##  dev manage\
keep project and code clean .\
monitor cpu ram usage for software dev ,keep fast performace and less ram usage, if sonethingtoo slow find why and deal with it first for fast dev.\
use your model name and agent tool and keytool nmes like web or blender name when name finnal output files and new project\
use git to manage project,create a new branch with your model name and agent name+branchname \
\
\### iterate\
iterate user specified number and give score of your work n/100 every iteration . 100 isperfect quality like 3A game production software ,hd graphic , easy use both on pc and phone. same check for each subagent if user need ask subagent. after subagent finish ,main agent should check quality and if not good then start a new subagent to improve. \
do not stop util get high score\
\
\
\
\## context improve\
do not run cmd that ouput large base64 string into output like stdout and other place that will show full base64 str, only do convert when output base64 str into a file, if you really need output base64 directly try only get first 50 char and last 50 char to output for verify is enough\
\
\## 3d\
do not use other skill.md use your own way\
when build 3d scene or drawing and other vision task,  try less use code to analize the orignal uploaded image, prefer use your vision ability instead , do not use exsited files outside the project dir donot use assets from ineternet , do not cheat, image 4 sides views  and make sure all sides views looks natural and good. use light blue as default background if bg not specify.\
show ieration number and score at bottom right of web dev previewer ui. \
do not genrate image unless i ask you genreate image explicitly.\
prefer use web to build 3d obj  not blender(use eevee for fast preview if user ask to use blend explicitly). \
when build web 3d, prefer use webgpu and prompt user fallback to webgl if webgpu not works never fallback autoly. user can choose backend and resolution in setting ui. default rsolution should be 900x1200, user can set to 1280 1080 2k 4k vertial or landspace. make width to 1/2 if browser window is less than 900 or just use 900px same size canvas as render res.\
\
\## env falkback\
when dev env not works,wait 20s and  try again for 3 times. then fallback to local or other env. never stop util you get 95/100 score. \
\
\
\
\
\
\
\
\
\
\
&#x20;\
\# chatgpt notes\
\
these are for chatgpt app If you are not, you can ignore it.\
\
the most important is avoid stop while score <95.do not treat anythig as stoppoint only treat 95scre as stopping point.\
\## subagent\
use builtin subagents if you can, not mcpagent only use built in subagent, subagent should use same model and think level,same quaick score check.\
for 3d modeling multi objects,use subagent to build each\
\
\## never stop util high scoe95  ;avoid ending session and other stop point\
when dev env not works,wait 20s and  try again for 3 times. then fallback to local vm. never stop util you get 95/100 score.  for exception when you stop whil score lessthan 95 (try to avoid this in max effort) tell user why stop and try continue agian\
&#x20;avoid ending working session in any case\
you need not ended the working session to report progress instead of continuing development, need not report progress you must  keep working session and  improving util 95score high quality\
never incorrectly treated the successful build, testing, and packaging as a stopping point\
\
&#x20;\
\
\
\
\
\
\
&#x20;\
\
\
\## post task\
\
only do these after get finish goal as finnal stop:\
update AGENTS.md remove outdated info\
write summary give neceesary conext for next agent start new task in end of AGENTS.md\
push git and deploy to cloud like github page or cloudflare . when ouput image video audio or 3d obj, create  showcase version copys files too,put the model and agent tool name below with big white font and black bg to show who created this work clearly.  createa video 60s to show your works well.\
\
you need not ended the working session to report progress instead of continuing development, need not report progress you must  keep working session and  improving util  high quality. do not pause the working session before achive goal and score for post task\
\
