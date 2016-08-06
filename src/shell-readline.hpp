#ifndef KYBD_READ_THREAD
#define KYBD_READ_THREAD
/**
 * This is the keyboard callable. 
 * This example implements a more
 * C++-y version of read-line.c.
 */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <functional>
#include <termios.h>
#include <algorithm>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <alloca.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <regex.h>
#include <fcntl.h>
#include <thread>
#include <vector>
#include <cctype>
#include <mutex>
#include <stack>

/** Include wildcards for tab completion **/
#include "wildcard.hpp"
#include "command.hpp"

#define STANDARD 572
#define SOURCE 5053

static int get_file = 0;

static class readLine
{
public:
  readLine() { m_get_mode = 1; }

  void operator() () {
    // Raw mode
    char input; int result;
    std::string _line = "";

    // Read in the next character
    for (; true ;) {
      if (!read(0, &input,  1)) {
		Command::currentCommand.printPrompt = true;
		std::cerr<<"Got sourced EOF!";
		// Change back to standard in!
		dup2(fdin, 0), close(fdin);
		continue;
      }

      // Echo the character to the screen
      if (input >= 32 && input != 127) {
		// Character is printable
		if (input == '!') {
		  result = write(0, "!", 1);
		  //Check for "!!" and "!-<n>"
		  if (!m_history.size()) {
			_line += input;
			continue;
		  }
	  
		  char ch1, ch2;
		  result = read(0, &ch1, 1);
		  if (ch1 == '!') {
			// "!!" = run prior command
			result =  write(1, "!", 1);
			_line += m_history[m_history.size() - 1];
			_line.pop_back();
			m_show_line = true;
			continue;
		  } else if (ch1 == '-') {
			result = write(1, "-", 1);
			auto && is_digit = [](char b) { return '0' <= b && b <= '9'; };
			// "!-<n>" = run what I did n commands ago.
			char * buff = (char*) alloca(20); char * b;
			for (b=buff;read(0,b,1)&&write(1,b,1)&&is_digit(*b);*(++b+1)=0);
			int n = atoi(buff); bool run_cmd = false;
			if (*b=='\n') run_cmd = true;
			if (n > 0) {
			  int _idx = m_history.size() - n;
			  _line += m_history[(_idx >= 0) ? _idx : 0];
			  _line.pop_back();
			  m_show_line = true;
			  if (run_cmd) {
				if (m_buff.size()) {
				  char ch;
				  for (; m_buff.size();) {
					ch = m_buff.top(); m_buff.pop();
					_line += ch;
					result = write(1, &ch, 1);
				  }
				}
				history_index = m_history.size();
				break;
			  }
			}
		  } else {
			result = write(1, &ch1, 1);
			_line += "!"; _line += ch1;
			continue;
		  }
		}
	
		if (m_buff.size()) {
		  /**
		   * If the buffer has contents, they are
		   * in the middle of a line.
		   */
		  _line += input;
	  
		  // Write current character
		  result = write(1, &input, 1);

		  // Copy buffer and print
		  std::stack<char> temp = m_buff;
		  for (char d = 0; temp.size(); ) {
			d = temp.top(); temp.pop();
			result = write(1, &d, 1);
		  }

		  // Move cursor to current position.
		  for (int x = 0, b = '\b'; x < m_buff.size(); ++x)
			result = write(1, &b, 1);
		} else {
		  _line += input;
		  if (history_index == m_history.size())
			m_current_line_copy += input;
		  // Write to screen
		  result = write(1, &input, 1);
		}
      } else if (input == 10) {
		// Enter was typed
		if (m_buff.size()) {
		  char ch;
		  for (; m_buff.size();) {
			ch = m_buff.top(); m_buff.pop();
			_line += ch;
			result = write(1, &ch, 1);
		  }
		}
		result = write(1, &input, 1);
		history_index = m_history.size();
		break;
      } else if (input == 1) {
		// Control A
		if (!_line.size()) continue;

		for (char d = '\b'; _line.size();) {
		  m_buff.push(_line.back()); _line.pop_back();
		  result = write(1, &d, 1);
		}
      } else if (input == 5) {
		// Control E

		for (char d = 0; m_buff.size();) {
		  d = m_buff.top(); m_buff.pop();
		  result = write(1, &d, 1);
		  _line += d;
		} 
      } else if (input == 4) {
		// Control D
		if (!m_buff.size()) continue;
		if (!_line.size()) {
		  m_buff.pop();
		  std::stack<char> temp = m_buff;
		  for (char d = 0; temp.size();)
			if (write(1, &(d = (temp.top())), 1)) temp.pop();
	  
		  if (!write(1, " ", 1)) std::cerr<<"WAT.\n";
		  for (int x = m_buff.size() + 1; x -= write(1, "\b", 1););
		  continue;
		}
	
		if (m_buff.size()) {
		  // Buffer!
		  std::stack<char> temp = m_buff;
		  temp.pop();
		  for (char d = 0; temp.size(); ) {
			d = temp.top(); temp.pop();
			result = write(1, &d, 1);
		  }
		  char b = ' ';
		  result = write(1, &b, 1); b = '\b';
		  result = write(1, &b, 1); m_buff.pop();
		  // Move cursor to current position.
		  for (int x = 0; x < m_buff.size(); ++x)
			result = write(1, &b, 1);
		} else continue;
		if (history_index == m_history.size()) m_current_line_copy.pop_back();
      } else if (input == 11) {
		/// Control K
		if (!m_buff.size()) continue;
		size_t count = m_buff.size() + 1;
		/// Clear the stack. On its own thread.
		std::thread stack_killer([this](){
			for(;m_buff.size();m_buff.pop());
		  }); stack_killer.detach();
        char * spaces = (char*) malloc(count + 1);
		char * bspaces = (char*) malloc(count + 1);
		memset(spaces, ' ', count); spaces[count] = '\0';
		memset(bspaces, '\b', count); bspaces[count] = '\0';
		if (write(1, spaces, count) != count) {
		  std::cerr<<"Could not write spaces to stdout"<<std::endl;
		} else if (write(1, bspaces, count) != count) {
		  std::cerr<<"count not write backspaces to stdout!"<<std::endl;
		}
      } else if (input == 8 || input == 127) {
		// backspace
		if (!_line.size()) continue;

		if (m_buff.size()) {
		  // Buffer!
		  char b = '\b';
		  result = write(1, &b, 1);
		  _line.pop_back();
		  std::stack<char> temp = m_buff;
		  for (char d = 0; temp.size(); ) {
			d = temp.top(); temp.pop();
			result = write(1, &d, 1);
		  }
		  b = ' ';
		  result = write(1, &b, 1); b = '\b';
		  result = write(1, &b, 1);
		  // Move cursor to current position.
		  for (int x = 0; x < m_buff.size(); ++x)
			result = write(1, &b, 1);
		} else {
		  char ch = '\b';
		  result = write(1, &ch, 1);
		  ch = ' ';
		  result = write(1, &ch, 1);
		  ch = '\b';
		  result = write(1, &ch, 1);
		  _line.pop_back();
		} if (history_index == m_history.size()) m_current_line_copy.pop_back();
      } else if (input == 9) {
		Command::currentSimpleCommand = std::unique_ptr<SimpleCommand>
		  (new SimpleCommand());
	
		// Part 1: add a '*' to the end of the stream.
		std::string _temp;
		// std::cerr<<std::endl;
		std::vector<std::string> _split;
		if (_line.size()) {
		  _split = string_split(_line, ' ');
		  _temp = _split.back() + "*";
		} else _temp = "*";

		char * _complete_me = strndup(_temp.c_str(), _temp.size());
	
		// Part 2: Invoke wildcard expand
		wildcard_expand(NULL, _complete_me);

		// Part 3: If wc_collector.size() <= 1, then complete the tab.
		//         otherwise, run "echo <text>*"
		std::string * array = Command::currentCommand.wc_collector.data();
		std::sort(array, array + Command::currentCommand.wc_collector.size(),
				  Comparator());

		if (Command::currentCommand.wc_collector.size() == 1) {
		  /* First check if the line has any spaces! */
		  /*     If so, we will wrap in quotes!      */
		  bool quote_wrap = false; 
		  if (Command::currentCommand.wc_collector[0].find(" ")
			  != std::string::npos) {
			Command::currentCommand.wc_collector[0].insert(0, "\"");
			quote_wrap = true;
		  }
		  char ch[_line.size() + 1]; char sp[_line.size() + 1];
		  ch[_line.size()] = '\0'; sp[_line.size()] = '\0';
		  memset(ch, '\b', _line.size()); memset(sp, ' ', _line.size());
		  int x = write(1, ch, _line.size());
		  x = write(1, sp, _line.size());
		  x = write(1, ch, _line.size());
		  _line = "";
	  
		  for (int x = 0; x < _split.size() - 1; _line += _split[x++] + " ");
		  _line += Command::currentCommand.wc_collector[0];
		  if (quote_wrap) _line = _line + "\"";
		  m_current_line_copy = _line;
		  Command::currentCommand.wc_collector.clear();
		  Command::currentCommand.wc_collector.shrink_to_fit();
		} else if (Command::currentCommand.wc_collector.size() == 0) {
		  continue;
		} else {
		  unsigned short x = 0; std::cout<<std::endl;
		  std::vector<std::string> _wcd = Command::currentCommand.wc_collector;
		  std::vector<std::string> cpyd = Command::currentCommand.wc_collector;
		  std::string longest_common((longest_substring(cpyd)));
		  
		  if (_wcd.size()) {
			printEvenly(_wcd); char * _echo = strdup("echo");
			Command::currentSimpleCommand->insertArgument(_echo);
			Command::currentCommand.wc_collector.clear();
			Command::currentCommand.wc_collector.shrink_to_fit();
			Command::currentCommand.execute(); free(_echo);

			/**
			 * Now we add the largest substring of
			 * the above to the current string so
			 * that the tab completion isn't "butts."
			 *
			 * ~ Ana-Gabriel Perez
			 */

			if (longest_common.size()) {
			  char * to_add = strndup(longest_common.c_str() + strlen(_complete_me) - 1,
									  longest_common.size() - strlen(_complete_me) + 1);
			  _line += to_add; free(to_add);
			  m_current_line_copy = _line;
			}
		  } else { free(_complete_me); continue; }
		} free(_complete_me);

		if (write(1, _line.c_str(), _line.size()) != _line.size()) {
		  perror("write");
		  std::cerr<<"I.E. STAHP!\n"<<std::endl;
		}
      } else if (input == 27) {	
		char ch1, ch2;
		// Read the next two chars
		result = read(0, &ch1, 1);
		result = read(0, &ch2, 1);
		if (ch1 == 91 && ch2 == 49) {
		  /// Control + arrow key?
		  char ch3[4]; memset(ch3, 0, 4 * sizeof(char));
		  const char * right = "\59\53\67";
		  const char * left  = "\59\53\68";
		  result = read(0, ch3, 3);
		  if (ch3[0] == 59 && ch3[1] == 53 && ch3[2] == 67) {
			/// control + right arrow
			/// If the stack is empty, we are done.
			if (!(m_buff.size())) continue;
			for (;(m_buff.size() &&
				   ((_line += m_buff.top(), m_buff.pop(),_line.back()) != ' ') &&
				   (write(1, &_line.back(), 1) == 1)) ||
				   ((_line.back() == ' ') ? !(write(1, " ", 1)) : 0););
	    
		  } else if (ch3[0] == 59 && ch3[1] == 53 && ch3[2] == 68) {
			/// control + left arrow
			if (!_line.size()) continue;
			for (;(_line.size() &&
				   ((m_buff.push(_line.back()),
					 _line.pop_back(), m_buff.top()) != ' ') &&
				   (write(1, "\b", 1) == 1)) ||
				   ((m_buff.top() == ' ') ? !(write(1, "\b", 1)) : 0););
		  }
		} else if (ch1 == 91 && ch2 == 51) {
		  // Maybe a delete key?
		  char ch3; result = read(0, &ch3, 1);
		  if (ch3 == 126) {
			if (!m_buff.size()) continue;
			if (!_line.size()) {
			  m_buff.pop();
			  std::stack<char> temp = m_buff;
			  for (char d = 0; temp.size();)
				if (write(1, &(d = (temp.top())), 1)) temp.pop();

			  if (!write(1, " ", 1)) std::cerr<<"WAT.\n";
			  for (int x = m_buff.size() + 1; x -= write(1, "\b", 1););
			  continue;
			}

			if (m_buff.size()) {
			  // Buffer!
			  std::stack<char> temp = m_buff;
			  temp.pop();
			  for (char d = 0; temp.size(); ) {
				d = temp.top(); temp.pop();
				result = write(1, &d, 1);
			  }
			  char b = ' ';
			  result = write(1, &b, 1); b = '\b';
			  result = write(1, &b, 1); m_buff.pop();
			  // Move cursor to current position.
			  for (int x = 0; x < m_buff.size(); ++x)
				result = write(1, &b, 1);
			} else continue;
			if (history_index == m_history.size()) m_current_line_copy.pop_back();
		  }
		} if (ch1 == 91 && ch2 == 65) {
		  // This was an up arrow.
		  // We will print the line prior from history.
		  if (!m_history.size()) continue;
		  // if (history_index == -1) continue;
		  // Clear input so far
		  char ch[_line.size() + 1]; char sp[_line.size() + 1];
		  memset(ch, '\b',_line.size()); memset(sp, ' ', _line.size());
		  result = write(1, ch, _line.size());
		  result = write(1, sp, _line.size());
		  result = write(1, ch, _line.size());

		  if (history_index == m_history.size()) --history_index;
		  // Only decrement if we are going beyond the first command (duh).
		  _line = m_history[history_index];
		  history_index = (!history_index) ? history_index : history_index - 1;
		  // Print the line
		  if (_line.size()) _line.pop_back();
		  result = write(1, _line.c_str(), _line.size());
		} if (ch1 == 91 && ch2 == 66) {
		  // This was a down arrow.
		  // We will print the line prior from history.
	  
		  if (!m_history.size()) continue;
		  if (history_index == m_history.size()) continue;
		  // Clear input so far
		  for (int x = 0, bsp ='\b'; x < _line.size(); ++x)
			result = write(1, &bsp, 1);
		  for (int x = 0, sp = ' '; x < _line.size(); ++x)
			result = write(1, &sp, 1);
		  for (int x = 0, bsp ='\b'; x < _line.size(); ++x)
			result = write(1, &bsp, 1);
	  
		  history_index = (history_index == m_history.size()) ? m_history.size()
			: history_index + 1;
		  if (history_index == m_history.size()) _line = m_current_line_copy;
		  else _line = m_history[history_index];
		  if (_line.size() && history_index != m_history.size()) _line.pop_back();
		  // Print the line
		  result = write(1, _line.c_str(), _line.size());
		} if (ch1 == 91 && ch2 == 67) {
		  // Right Arrow
		  if (!m_buff.size()) continue;
		  char wrt = m_buff.top();
		  result = write(1, &wrt, 1);
		  _line += wrt; m_buff.pop();
		} if (ch1 == 91 && ch2 == 68) {
		  if (!_line.size()) continue;
		  // Left Arrow
		  m_buff.push(_line.back());
		  char bsp = '\b';
		  result = write(1, &bsp, 1);
		  _line.pop_back();
		}
      }
      
    }

    _line += (char) 10 + '\0';
    m_current_line_copy.clear();
    m_history.push_back(_line);
  }

  char * getStashed() {
    char * ret = (char*) calloc(m_stashed.size() + 1, sizeof(char));
    strncpy(ret, m_stashed.c_str(), m_stashed.size());
    ret[m_stashed.size()] = '\0';
    m_get_mode = 1;
    std::cerr<<"get returning: "<<ret<<std::endl;
    return ret;
  }
  
  char * get() {
    if (m_get_mode == 2) {
      char * ret = (char*) calloc(m_stashed.size() + 1, sizeof(char));
      strncpy(ret, m_stashed.c_str(), m_stashed.size());
      ret[m_stashed.size()] = '\0';
      m_get_mode = 1;
      std::cerr<<"get returning: "<<ret<<std::endl;
      return ret;
    }
      
    std::string returning;
    returning = m_history[m_history.size() - 1];
    if (m_mode == SOURCE) m_history.pop_back();
    char * ret = (char*) calloc(returning.size() + 1, sizeof(char));
    strncpy(ret, returning.c_str(), returning.size());
    ret[returning.size()] = '\0';
    return ret;
  }

  void tty_raw_mode() {
    tcgetattr(0,&oldtermios);
    termios tty_attr = oldtermios;
    /* Set raw mode. */
    tty_attr.c_lflag &= (~(ICANON|ECHO));
    tty_attr.c_cc[VTIME] = 0;
    tty_attr.c_cc[VMIN] = 1;
    
    tcsetattr(0,TCSANOW,&tty_attr);
  }

  void unset_tty_raw_mode() {
    tcsetattr(0, TCSAFLUSH, &oldtermios);
  }

  void setMode(const int & _mode) { m_mode = _mode; }

  void setFile(const std::string & _filename) {
    m_filename = _filename;
    char * _name = strndup(_filename.c_str(), _filename.size());
    int _fd = open(_name, O_RDONLY); fdin = dup(0);
    dup2(_fd, 0); close(_fd);
    Command::currentCommand.printPrompt = false;
  }

private:
  std::vector<std::string> string_split(std::string s, char delim) {
    std::vector<std::string> elems; std::stringstream ss(s);
    std::string item;
    for (;std::getline(ss, item, delim); elems.push_back(std::move(item)));
    return elems;
  }
  std::string m_current_path;
  std::string m_current_line_copy;
  std::string m_stashed;
  std::stack<char> m_buff;
  std::vector<std::string> m_path;
  std::vector<std::string> m_history;
  ssize_t history_index = 0;
  int fdin = 0;
  std::string m_filename;
  std::ifstream * m_ifstream;
  volatile int m_mode = STANDARD;
  int m_get_mode;
  bool m_show_line = false;
  termios oldtermios;
} reader;
#endif
