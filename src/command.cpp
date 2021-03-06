#include "command.hpp"

std::vector<int> m_background;

SimpleCommand::SimpleCommand() { /** Used to call malloc for five stuffs here **/ }

void SimpleCommand::insertArgument(char * argument)
{
   std::string as_string(argument);

   auto exists = Command::currentCommand.m_aliases.find(as_string);

   /* exists == m_aliases.end() => we are inserting an alias */
   if (exists != Command::currentCommand.m_aliases.end()) {
	  auto to_insert = exists->second;
	  for (auto && str : to_insert) {
		 char * toPush = strndup(str.c_str(), str.size());
		 arguments.push_back(toPush); ++numOfArguments;
	  }
   } else {
	  std::string arga = tilde_expand(as_string);
	  std::string arg  = env_expand(arga);

	  char * str = strndup(arg.c_str(), arg.size());
	  int index; char * t_str = str;
	  char * temp = (char*) calloc(arg.size() + 1, sizeof(char));

	  for (index = 0; *str; ++str) {
		 if (*str == '\\' && *(str + 1) == '\"') {
			temp[index++] = '\"'; ++str;
		 } else if (*str == '\\' && *(str + 1) == '&') {
			temp[index++] = '&'; ++str;
		 } else if (*str == '\\' && *(str + 1) == '#') {
			temp[index++] = '#'; ++str;
		 } else if (*str == '\\' && *(str + 1) == '<') {
			temp[index++] = '<'; ++str;
		 } else if (*str == '\\' && *(str + 1) == '\\' && *(str+2) == '\\') {
			temp[index++] = '\\'; ++str; ++str;
		 } else if (*str == '\\' && *(str + 1) == '\\'){
			temp[index++] = '\\'; ++str;
		 } else if (*str == '\\' && *(str + 1) == ' ') {
			temp[index++] = ' '; ++str;
		 } else {
			temp[index++] = *str;
		 }
	  } free(t_str);
	  char * toPush = new char[index + 1]; memset(toPush, 0, index + 1);
	  strcpy(toPush, temp);
	  arguments.push_back(toPush), ++numOfArguments;
	  free(temp);
   }
}

inline int eval_to_buffer(char * const* cmd, char * outBuff, size_t buffSize)
{
   int fdpipe[2]; int pid = -1; size_t x = 0;

   if (pipe(fdpipe) < 0) return -1;
   else if ((pid = fork()) < 0) return -1;
    else if (pid == 0) {
	  /** Child Process: write into the pipe **/
	  close(fdpipe[0]);   // close unused read end
	  dup2(fdpipe[1], 1); // stdout to pipe
	  dup2(fdpipe[1], 2); // stderr to pipe
	  close(fdpipe[1]);

	  if (execvp(cmd[0], cmd)) return -1;
	  _exit(0);
   } else {
	  /** Parent Process: read from the pipe **/
	  close(fdpipe[1]);   // close unused write end
	  for (memset(outBuff, 0, buffSize);x = read(fdpipe[0], outBuff, buffSize););
	  if (x == buffSize) return -1;
	  waitpid(pid, NULL, 0);
   } return 0;
}

#define SUBSH_MAX_LEN 4096
void Command::subShell(char * arg)
{
   std::cerr<<"Running subshell cmd: \""<<arg<<"\""<<std::endl;
   int cmd_pipe[2]; int out_pipe[2]; pid_t pid;
   int tmpin = dup(0); int tmpout = dup(1); int tmperr = dup(2);
  
   if (pipe(cmd_pipe) == -1) {
	  perror("cmd_pipe");
	  return;
   } else if (pipe(out_pipe) == -1) {
	  perror("out_pipe");
	  return;
   }

   dup2(cmd_pipe[1], 1); close(cmd_pipe[1]); /* cmd to stdout */
   dup2(out_pipe[0], 0); close(out_pipe[0]); /* out to stdin  */
  
   if ((pid = fork()) == -1) {
	  perror("subshell fork");
	  return;
   } else if (pid == 0) {
	  /* Child Process */
	  close(out_pipe[0]); /* close the read end of the out pipe */
	  close(cmd_pipe[1]); /* close the write end of the cmd pipe */

	  dup2(out_pipe[1], 1); close(out_pipe[1]); /* out_pipe[1] -> stdout */
	  dup2(cmd_pipe[0], 0); close(cmd_pipe[0]); /* cmd_pipe[0] -> stdin  */

	  execlp("yash", "yash", NULL);
	  perror("subshell exec");
	  _exit(1);
   } else if (pid != 0) {
	  /* Parent Process */
	  char * buff = (char*) calloc(SUBSH_MAX_LEN, sizeof(char));
	  char * c = NULL;
	
	  close(out_pipe[1]); /* close the write end of the out pipe */
	  close(cmd_pipe[0]); /* close the read end of the cmd pipe */

	  /* write the command to the write end of the cmd pipe */
	  for (c = arg; *c && write(cmd_pipe[1], c++, 1););

	  /* Close pipe so subprocess isn't waiting */
	  dup2(tmpout, 1); close(tmpout); close(cmd_pipe[1]);
	
	  waitpid(pid, NULL, WNOHANG); /* Don't hang if child is already dead */

	  //std::cerr<<"Child rage quit"<<std::endl;
	
	  /* read from the out pipe and store in a buffer */
	  for (c = buff; read(out_pipe[0], c++, 1););

	  std::cerr<<"Read from buffer"<<std::endl;
	  size_t buff_len = c - buff; /* this is the number of characters read */

	  /* Push the buffer onto stdin */
	  for (int b = 0; (ungetc(buff[b++], stdin)) && buff_len--;);
	  free(buff); /* release the buffer */
   }

   /* restore default IO */
   dup2(tmpin, 0); dup2(tmpout, 1); dup2(tmperr, 2);
}

Command::Command()
{ /** Constructor **/ }

void Command::insertSimpleCommand(std::shared_ptr<SimpleCommand> simpleCommand)
{ simpleCommands.push_back(simpleCommand), ++numOfSimpleCommands; }

void Command::clear()
{
   simpleCommands.clear(),
	  background = append = false,
	  numOfSimpleCommands = 0,
	  outFile.release(), inFile.release(),
	  errFile.release(), simpleCommands.shrink_to_fit();
   outSet = inSet = errSet = false;
}

void Command::print()
{
   std::cout<<std::endl<<std::endl;
   std::cout<<"              COMMAND TABLE                "<<std::endl;  
   std::cout<<std::endl; 
   std::cout<<"  #   Simple Commands"<<std::endl;
   std::cout<<"  --- ----------------------------------------------------------"<<std::endl;

   for (int i = 0; i < numOfSimpleCommands; i++) {
	  printf("  %-3d ", i);
	  for (int j = 0; j < simpleCommands[i]->numOfArguments; j++) {
		 std::cout<<"\""<< simpleCommands[i]->arguments[j] <<"\" \t";
	  }
   }

   std::cout<<std::endl<<std::endl;
   std::cout<<"  Output       Input        Error        Background"<<std::endl;
   std::cout<<"  ------------ ------------ ------------ ------------"<<std::endl;
   printf("  %-12s %-12s %-12s %-12s\n", outFile.get()?outFile.get():"default",
		  inFile.get()?inFile.get():"default", errFile.get()?errFile.get():"default",
		  background?"YES":"NO");
   std::cout<<std::endl<<std::endl;
}


void Command::execute()
{
  // Don't do anything if there are no simple commands
  if (numOfSimpleCommands == 0) {
	prompt();
	return;
  }

  // Print contents of Command data structure
  char * dbg = getenv("SHELL_DBG");
  if (dbg && !strcmp(dbg, "YES")) print();

  char * lolz = getenv("LOLZ");
  if (lolz && !strcmp(lolz, "YES")) {
	/// Because why not?
	std::shared_ptr<SimpleCommand> lul(new SimpleCommand());
	char * _ptr = strdup("lolcat");
	lul->insertArgument(_ptr);
	free(_ptr);
	if (strcmp(simpleCommands.back().get()->arguments[0], "cd") &&
		strcmp(simpleCommands.back().get()->arguments[0], "clear") &&
		strcmp(simpleCommands.back().get()->arguments[0], "ssh") &&
		strcmp(simpleCommands.back().get()->arguments[0], "setenv") &&
		strcmp(simpleCommands.back().get()->arguments[0], "unsetenv")) {
	  this->insertSimpleCommand(lul);
	}
  }
  
   // Add execution here
   // For every simple command fork a new process
   int pid = -1;
   int fdpipe [2]; //for de pipes
   int fderr, fdout, fdin; char * blak;
   int tmpin  = dup(0);
   int tmpout = dup(1);
   int tmperr = dup(2);

   // Redirect the input
   if (inFile.get()) {
	  fdin  = open(inFile.get(),  O_WRONLY, 0700);
   } else fdin = dup(tmpin);

   if (outFile.get()) {
	  if (this->append) fdout = open(outFile.get(), O_CREAT | O_WRONLY | O_APPEND, 0600);
	  else fdout = open(outFile.get(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
   } else fdout = dup(tmpout);

   if (errFile.get()) {
	  if (this->append) fderr = open(errFile.get(), O_CREAT | O_WRONLY | O_APPEND, 0600);
	  else fderr = open(errFile.get(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
   } else fderr = dup(tmperr);

   if (fderr < 0) {
	  perror("error file");
	  exit(1);// could not open file
   } if (fdout < 0) {
	  perror("out file");
	  exit(1);
   } if (fdin < 0) {
	  perror("in file");
	  exit(1);
   }

   // Redirect stderr to file
   dup2(fderr, 2);
   close(fderr);
   for (int x = 0; x < numOfSimpleCommands; ++x) {
	  std::vector<char *> curr = simpleCommands.at(x).get()->arguments;
	  curr.push_back((char*) NULL);
	  char ** d_args;
	  d_args = curr.data();
	
	  dup2(fdin, 0);
	  close(fdin);
	
	  /** output direction **/
	  if (x == numOfSimpleCommands - 1) {
		 if (outFile.get()) {
			if (this->append) fdout = open(outFile.get(), O_CREAT|O_WRONLY|O_APPEND, 600);
			else fdout = open(outFile.get(), O_CREAT|O_WRONLY|O_TRUNC, 0600);
		 } else fdout = dup(tmpout);

		 if (errFile.get()) {
			if (this->append) fderr = open(outFile.get(), O_CREAT|O_WRONLY|O_APPEND, 600);
			else fderr = open(errFile.get(), O_CREAT|O_WRONLY|O_TRUNC, 0600);
		 } else fderr = dup(tmperr);
	  } // direct to requested upon last command

	  else {
		 int fdpipe[2];
		 int pipe_res = pipe(fdpipe);
		 fdout = fdpipe[1];
		 fdin  = fdpipe[0];
	  }

	  dup2(fdout, 1);
	  close(fdout);

	  if (d_args[0] == std::string("cd")) {
		 std::string curr_dir = std::string(getenv("PWD"));
		 int cd; std::string new_dir;

		 if (curr.size() == 4) {
			char * to_replace = strdup(d_args[1]);
			char * replace_to = strdup(d_args[2]);		
			char * replace_in = strndup(curr_dir.c_str(), curr_dir.size());
		
			char * sub = strstr(replace_in, to_replace);

			/* Desired replacement wasn't found, so error and exit */
			if (sub == NULL) {
			   perror("cd");

			   free(to_replace);
			   free(replace_to);
			   free(replace_in);
			   
			   dup2(tmpin, 0);  close(tmpin);
			   dup2(tmpout, 1); close(tmpout);
			   dup2(tmperr, 2); close(tmperr);
		  
			   clear(); prompt(); return;
			}

			register size_t replace_len     = strlen(to_replace);
			register size_t replacement_len = strlen(replace_to);

			if (!(replace_len && replacement_len)) {
			   std::cerr<<"Error: replacement cannot be empty!"<<std::endl;

			   free(to_replace); free(replace_in); free(replace_to);
			   
			   dup2(tmpin, 0);  close(tmpin);
			   dup2(tmpout, 1); close(tmpout);
			   dup2(tmperr, 2); close(tmperr);
		  
			   clear(); prompt(); return;
			}
			/* garauntee we have enough space */
			char * replacement = (char*) calloc(curr_dir.size() -
												replace_len +
												replacement_len + 1,
												sizeof(char));
			char * d = replacement;
			/* copy up to the beginning of the substring */
			for (char * c = replace_in; c != sub; *(d++) = *(c++));
			/* copy the replacement */
			for (char * c = replace_to; *c; *(d++) = *(c++));
			/* advance past the substring we wanted to replace */
			char * residual = sub + replace_len;
			/* copy the residual chars over */
			for (; *residual; *(d++) = *(residual++));
		
			std::cout<<replacement<<std::endl;
		
			free(to_replace); free(replace_to); free(replace_in);

			std::string new_dir(replacement); free(replacement);
			if (!changedir(new_dir)) {
			   perror("cd");
		  
			   dup2(tmpin, 0);  close(tmpin);
			   dup2(tmpout, 1); close(tmpout);
			   dup2(tmperr, 2); close(tmperr);
		  
			   clear(); prompt(); return;
			}
		 } else if (curr.size() == 2) {
			std::string _empty = "";
			if (!changedir(_empty)) {
			   perror("cd");
		  
			   dup2(tmpin, 0);
			   dup2(tmpout, 1);
			   dup2(tmperr, 2);
			   close(tmpin);
			   close(tmpout);
			   close(tmperr);

			   clear(); prompt();
			}
		
			setenv("PWD", getenv("HOME"), 1);
		 } else {
			if (d_args[1] == std::string("pwd") ||
				d_args[1] == std::string("/bin/pwd")) {
			} else if (*d_args[1] != '/') { 
			   new_dir = std::string(getenv("PWD"));
			   for (;new_dir.back() == '/'; new_dir.pop_back());
			   new_dir += "/" + std::string(d_args[1]);
			} else new_dir = std::string(d_args[1]);

			for (; *new_dir.c_str() != '/' && new_dir.back() == '/'; new_dir.pop_back());
			if (changedir(new_dir)) setenv("PWD", new_dir.c_str(), 1);
		 }
		 setenv("PWD", curr_dir.c_str(), 1);
		 // Regardless of errors, cd has finished.
	  } else if (d_args[0] == std::string("ls")) {
		 char ** temp = new char*[curr.size() + 2];
		 for (int y = 2; y < curr.size(); ++y) {
			temp[y] = strdup(curr[y - 1]);
		 } // ... still better than managing myself!
		 temp[0] = strdup("ls");
		 temp[1] = strdup("--color=auto");
		 temp[curr.size()] = NULL;

		 pid = fork();

		 if (pid == 0) {
			execvp (temp[0], temp);
			perror("execvp");
			exit(2);
		 } 
		 for (int x = 0; x < curr.size() + 1; ++x) {
			free(temp[x]);
			temp[x] = NULL;
		 } delete[] temp;
	  } else if (d_args[0] == std::string("setenv")) {
		 char * temp = (char*) calloc(strlen(d_args[1]) + 1, sizeof(char));
		 char * pemt = (char*) calloc(strlen(d_args[2]) + 2, sizeof(char));
		 strcpy(temp, d_args[1]); strcpy(pemt, d_args[2]);
		 int result = setenv(temp, pemt, 1);
		 if (result) perror("setenv");
		 clear(); prompt(); free(temp); free(pemt);
		 return;
	  } else if (d_args[0] == std::string("unsetenv")) {
		 char * temp = (char*) calloc(strlen(d_args[1]) + 1, sizeof(char));
		 strcpy(temp, d_args[1]);
		 if (unsetenv(temp) == -1) perror("unsetenv");
		 clear(); prompt(); free(temp);
		 return;
	  } else if (d_args[0] == std::string("grep")) {
		 char ** temp = new char*[curr.size() + 1];
		 for (int y = 0; y < curr.size() - 1; ++y) {
			temp[y] = strdup(curr[y]);
		 } // ... still better than managing myself!
		 temp[curr.size() - 1] = strdup("--color");
		 temp[curr.size()] = NULL;

		 pid = fork();

		 if (pid == 0) {
			execvp (temp[0], temp);
			perror("execvp");
			exit(2);
		 } for (int x = 0; x < curr.size(); ++x) {
			free(temp[x]); temp[x] = NULL;
		 }
	  } else {
		 // Time for a good hot fork
		 pid = fork();

		 if (pid == 0) {
			if (d_args[0] == std::string("printenv")) {
			   char ** _env = environ;
			   for (; *_env; ++_env) std::cout<<*_env<<std::endl;
			   _exit (0);
			}

			execvp (d_args[0], d_args);
			perror("execvp");
			_exit(1);
		 }
	  }
   }

   // Restore I/O defaults

   dup2(tmpin, 0);  close(tmpin);
   dup2(tmpout, 1); close(tmpout);
   dup2(tmperr, 2); close(tmperr);

   if (!background) waitpid(pid, NULL, 0);
   else m_background.push_back(pid);

   /* Clear to prepare for next command */
   clear();

   /* Print new prompt if we are in a terminal. */
   if (isatty(0)) prompt();
}

// Shell implementation

void Command::prompt()
{
   if (!printPrompt) return;
   std::string PROMPT; char * pmt = getenv("PROMPT");
   if (pmt) PROMPT = std::string(pmt);
   else PROMPT = std::string("default");

   if (isatty(0) && PROMPT == std::string("default")) {
	  std::string _user = std::string(getenv("USER"));
	  char buffer[100]; std::string _host;
	  if (!gethostname(buffer, 100)) _host = std::string(buffer);
	  else _host = std::string("localhost");
	  char * _wd = NULL, * _hme = NULL;
	  char cdirbuff[2048]; char * const _pwd[2] = { (char*) "pwd", (char*) NULL };
	  eval_to_buffer(_pwd, cdirbuff, 2048);
	  std::string _cdir = std::string(cdirbuff);
	  char * _curr_dur = strndup(_cdir.c_str(), _cdir.size() - 1);
	  if (setenv("PWD", _curr_dur, 1)) perror("setenv");
	  std::string _home = std::string(_hme = getenv("HOME"));

	  // Replace the user home with ~
	  if (strstr(_curr_dur, _hme)) _cdir.replace(0, _home.size(), std::string("~"));

	  if (_user != std::string("root")) {
		 std::cout<<"\x1b[36;1m"<<_user<<"@"<<_host<<" ";
		 std::cout<<"\x1b[33;1m"<<_cdir<<"$ "<<"\x1b[0m";
		 fflush(stdout);
	  } else {
		 // Root prompt is cooler than you
		 std::cout<<"\x1b[31;1m"<<_user<<"@"<<_host<<" ";
		 std::cout<<"\x1b[35;1m"<<_cdir<<"# "<<"\x1b[0m";
		 fflush(stdout);
	  } free(_curr_dur);
   } else fflush(stdout);
}

Command Command::currentCommand;
std::shared_ptr<SimpleCommand> Command::currentSimpleCommand;

int yyparse(void);

void ctrlc_handler(int signum)
{
   /** kill my kids **/
   kill(signum, SIGINT);
}

void sigchld_handler(int signum)
{
   int saved_errno = errno; int pid = -1;
   for(; pid = waitpid(-1, NULL, WNOHANG) > 0;) {
	  bool found = false;
	  for (auto && a : m_background) {
		 if (a == pid) {
			found = true;
			break;
		 }
	  } if (found) {
		 std::cout<<"["<<signum<<"] has exited."<<std::endl;
		 Command::currentCommand.prompt();
	  }
   } errno = saved_errno;
}

std::vector<std::string> splitta(std::string s, char delim) {
   std::vector<std::string> elems; std::stringstream ss(s);
   std::string item;
   for (;std::getline(ss, item, delim); elems.push_back(std::move(item)));
   return elems;
}

void Command::setAlias(const char * _from, const char * _to)
{
   std::string from(_from); std::string to(_to);
   std::vector<std::string> split = splitta(to, ' ');
	
   /**
	* We really don't care if the alias has been
	* set. We should just overwrite the current
	* alias. So, we just use the [] operator
	* in map.
	*/

   m_aliases[from] = split;
}

void Command::pushDir(const char * new_dir) {
   char * _pwd = getenv("PWD");
   
   if (_pwd == NULL) {
	  perror("pwd");
	  return;
   } else if (new_dir == NULL || *new_dir == '\0') {
	  std::cerr<<"Invalid new directory!"<<std::endl;
	  return;
   }
   
   std::string curr_dir = std::string(getenv("PWD"));
   std::string news(new_dir);

   news = tilde_expand(news);
   if(news.find_first_of("*") != std::string::npos) news = curr_dir + "/" + news;

   wildcard_expand((char*)news.c_str());
   
   if(!wc_collector.size() && changedir(news)) {
      m_dir_stack.insert(m_dir_stack.begin(), curr_dir);
   } else if(wc_collector.size()) {
	  
	  for (int y = wc_collector.size() - 1; y--; ) {
		 auto x = wc_collector[y];
		 if (is_directory(x)) m_dir_stack.insert(m_dir_stack.begin(), x);
	  } if (m_dir_stack.size()) {
		 changedir(m_dir_stack[0]);
		 m_dir_stack.erase(m_dir_stack.begin(), m_dir_stack.begin() + 1);
		 m_dir_stack.push_back(curr_dir);
	  } else goto clear_and_exit;
   } else goto clear_and_exit;
   
   for(auto && a: m_dir_stack) std::cout<<a<<" ";
   if(!m_dir_stack.empty()) std::cout<<std::endl;
clear_and_exit:
   wc_collector.clear();
   wc_collector.shrink_to_fit();
}

void Command::popDir() {
   if (!m_dir_stack.size()) {
	  std::cerr<<"No directories left to pop!"<<std::endl;
	  return;
   }
   
   std::string dir = tilde_expand(m_dir_stack.front());
   if(changedir(dir)) {
      m_dir_stack.erase(m_dir_stack.begin(), m_dir_stack.begin()+1);
   }
   for(auto && a: m_dir_stack) std::cout<<a<<" ";
   std::cout<<std::endl;
}
