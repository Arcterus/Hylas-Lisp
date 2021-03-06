/*!
 * @file fndef.cpp
 * @brief Implementation of function definition and calling.
 * @author Eudoxia
 */
  
namespace Hylas
{
  using namespace std;
  using namespace llvm;
  
  void print(map<string,string> in)
  {
    printf("{\n");
    for(map<string,string>::iterator i = in.begin(); i != in.end(); i++)
    {
      printf("\t%s : %s\n", i->first.c_str(), i->second.c_str());
    }
    printf("}\n");
  }
  
  void print_metafunction(string in)
  {
    for(unsigned int i = 0; i < FunctionTable[in].versions.size(); i++)
    {
      printf("\t\t{\n");
      printf("\t\t\t Name: %s\n",FunctionTable[in].versions[i].name.c_str());
      printf("\t\t\t Return type: %s\n",FunctionTable[in].versions[i].ret_type.c_str());
      printf("\t\t\t Function Pointer Type: %s\n",FunctionTable[in].versions[i].fn_ptr_type.c_str());
      printf("\t\t\t Number of Arguments: %lu\n",FunctionTable[in].versions[i].nargs);
      printf("\t\t\t Tail Call Optimized?: %s\n",FunctionTable[in].versions[i].tco ? "Yes" : "No");
      printf("\t\t}\n");
    }
  }
  
  void print_fntable()
  {
    printf("{\n");
    for(map<string,MetaFunction>::iterator i = FunctionTable.begin(); i != FunctionTable.end(); i++)
    {
      printf("\t%s: \n",i->first.c_str());
      print_metafunction(i->first);
    }
    printf("}\n");
  }
  
  void validate_function(Form* form)
  {
    const static unsigned int name_pos = 1;
    const static unsigned int args_pos = 3;
    if(length(form) < 2)
      error(form,"Can't define a function with no name.");
    else if(length(form) < 3)
      error(form,"Can't define a function with no return type.");
    else if(length(form) < 4)
      error(form,"Can't define a function with no argument list.");
    if(length(form) < 5)
      error(form,"Can't define a function with no body (The block of code to execute).");
    if(tag(nth(form,name_pos)) != Atom)
      error(form,"Can't use a list as a function name.");
    else if(analyze(val(nth(form,name_pos))) != Symbol)
      error(form,"form,Can't use a non-symbolic atom as a function name.");
    if(nth(form,args_pos) != NULL)
      if(tag(nth(form,args_pos)) != List)
        error(form,"The argument list must be a list.");
  }
  
  string removeReturn(string in)
  {
    string out;
    if(in.find("(") != string::npos)
      out = string(in,in.find("("));
    else
      nerror("Couldn't find a return type to remove from the pointer signature '",in,"'.");
    return out;
  }
  
  string defineFunction(Form* form, fnType fn_type, bool inlining)
  {
    //0                                                1      2          3             [3]         3/4 (Depending on the docstring)
    //(function|fast|recursive|inline|recursive-inline [name] [ret_type] (arg1, arg1, ..., argn) "docstring" [body]*
    const static unsigned int name_pos = 1;
    const static unsigned int ret_type_pos = 2;
    const static unsigned int args_pos = 3;
    const static unsigned int body_starting_pos = 4; 
    validate_function(form);
    Lambda newfn;
    if(fn_type == Function) { newfn.fastcc = false; newfn.tco = false; newfn.lining = inlining; }
    else if(fn_type == Fast) { newfn.fastcc = true; newfn.tco = false; newfn.lining = inlining; }
    else if(fn_type == Recursive) { newfn.fastcc = true; newfn.tco = true; newfn.lining = inlining; }
    string fn_name = val(nth(form,name_pos));
    newfn.ret_type = printTypeSignature(nth(form,ret_type_pos));
    /*map<string,Lambda>::iterator macfind = Macros.find(fn_name);
    if(macfind != Macros.end())
      { printf("ERROR: A macro has already been defined with that name."); Unwind(); }*/
    Scope new_scope;
    master.SymbolTable.push_back(new_scope);
    //Iterate over arguments
    map<string,string> fn_args;
    newfn.fn_ptr_type = newfn.ret_type + "(";
    Form* current_arg;
    if(nth(form,args_pos) != NULL)
    {
      for(unsigned long i = 0; i < length(nth(form,args_pos)); i++)
      {
        current_arg = nth(nth(form,args_pos),i);
        if(tag(current_arg) == List)
        {
          if(tag(nth(current_arg,0)) != Atom)
          {
            printf("ERROR: An argument name must be a symbol, not a list or any other kind of atom.");
            Unwind();
          }
          else
          {
            string argname = val(nth(current_arg,0));
            map<string,string>::iterator seeker = fn_args.find(argname);
            if(seeker != fn_args.end())
            {
              printf("ERROR: Argument name '%s' was already defined in this argument list:\n%s",
                     argname.c_str(),print(nth(form,2)).c_str());
            }
            else
            {
              string type = printTypeSignature(nth(current_arg,1));
              fn_args[argname] = type;
              master.SymbolTable[ScopeDepth].vars[argname].type = type;
              master.SymbolTable[ScopeDepth].vars[argname].constant = false;
              master.SymbolTable[ScopeDepth].vars[argname].global = false;
              master.SymbolTable[ScopeDepth].vars[argname].regtype = LValue;
              newfn.fn_ptr_type += type + ",";
            }
          }
        }
        else
        { 
          if(val(current_arg) == "...")
          {
            fn_args[gensym()] = "...";
            newfn.fn_ptr_type += "...,";
          }
        }
      }
    }
    newfn.arguments = fn_args;
    newfn.nargs = fn_args.size();
    newfn.fn_ptr_type = ((nth(form,args_pos) == NULL)?newfn.fn_ptr_type:cutlast(newfn.fn_ptr_type)) + ")*";
    string rem = removeReturn(newfn.fn_ptr_type);
    string tmp_code;
    map<string,MetaFunction>::iterator seeker = FunctionTable.find(fn_name);
    if(seeker != FunctionTable.end())
    {
      for(unsigned int i = 0; i < seeker->second.versions.size(); i++)
      {
        if(removeReturn(seeker->second.versions[i].fn_ptr_type) == rem) //Compare prototypes without comparing return types
          error(form,"A function with the same prototype (",cutlast(rem),") has already been defined.");
      }
      newfn.name = fn_name + to_string(seeker->second.versions.size());
      seeker->second.versions.push_back(newfn);
    }
    else
    {
      newfn.name = fn_name + "0";
      MetaFunction new_metafn;
      new_metafn.versions.push_back(newfn);
      FunctionTable[fn_name] = new_metafn;
    }
    string arg_name, base_name, arg_code, out;
    for(map<string,string>::reverse_iterator i = fn_args.rbegin(); i != fn_args.rend(); i++)
    {
      if(i->second == "...")
      {
        arg_code += "...,";
        //TODO: Check that it's the last one on the list
      }
      else
      {
        arg_name = "%" + i->first+to_string(ScopeDepth);
        base_name = arg_name + "_base";
        arg_code += i->second + " " + base_name + ",";
        tmp_code += allocate(arg_name,i->second);
        tmp_code += store(i->second,base_name,arg_name);
      }
    }
    string processed_name = "@" + fn_name + ((seeker != FunctionTable.end()) ? to_string(seeker->second.versions.size()-1) : "0");
    tmp_code = (string)"define " + (newfn.fastcc? "fastcc ":"ccc ") + newfn.ret_type + " " + processed_name
        + "(" + cutlast(arg_code) + ")" + (newfn.lining? " alwaysinline" : "") + "\n{\n" + tmp_code;
    //Compile the code
    for(unsigned long i = body_starting_pos; i < length(form);i++)
      tmp_code += emitCode(nth(form,i));
    if(latest_type() != newfn.ret_type)
      error(form,"The return type of the function must match the type of the last form in it.");
    master.SymbolTable.erase(master.SymbolTable.end());
    push(tmp_code + "ret " + newfn.ret_type + " " + get_current_res() + "\n}");
    out += constant(get_unique_res(newfn.fn_ptr_type),newfn.fn_ptr_type,processed_name);
    return out;
  }
  
  string callGeneric(long gen_pos, Form* code)
  {
    //Not implemented yet
    return print(code) + to_string(gen_pos);
  }
  
  string cleanPointer(string in)
  {
    //i32(i8*,i64)* -> (i8*,i64)
    string arglist = cutlast(string(in,in.find('(')));
    return arglist;
  }
  
  string callFunction(Form* in)
  {
    //print_fntable();
    //First of all: What does the function look like? Get a "partial function pointer": put the argument types into a list
    string pointer, out;
    string arguments = "(";
    if(cdr(in) == NULL)
    {
      arguments = "()";
      pointer = "()";
    }
    else
    {
      pointer = "(";
      for(unsigned long i = 1; i < length(in); i++)
      {
        out += emitCode(nth(in,i));
        pointer += latest_type() + ",";
        arguments += latest_type() + " " + get_current_res() + ",";
      }
      pointer = ((length(in) != 1) ? cutlast(pointer) + ")" : pointer + ")");
      arguments = ((length(in) != 1) ? cutlast(arguments) + ")" : arguments + ")");
    }
    if(isatom(car(in)))
    {
      //Calling a function by name
      //First, find the name in the table
      string name = val(car(in));
      map<string,MetaFunction>::iterator seeker = FunctionTable.find(name);
      if(seeker != FunctionTable.end())
      {
        //Name found, now we compare our pointer to each version's pointer sans the return type
        for(unsigned long i = 0; i < seeker->second.versions.size(); i++)
        {
          //cerr << "Pointer: " << pointer << endl;
          string target = cleanPointer(seeker->second.versions[i].fn_ptr_type);
          //cerr << "Pointer compared: " << target << endl;
          //cerr << "HERP" << string(pointer,0,target.find("...")-1) << endl;
          //cerr << "HERP" << string(target,0,target.find("...")-1) << endl;
          if((pointer == target) ||
             (string(pointer,0,target.find("...")-1) == string(target,0,target.find("...")-1)))
          {
            string ret_type = seeker->second.versions[i].ret_type;
            //Found our match, emit code to call the function
            out += ((ret_type != "void") ? get_unique_res(seeker->second.versions[i].ret_type) + " = ":"") + (seeker->second.versions[i].tco ? "tail call " : "call ");
            out += (seeker->second.versions[i].fastcc ? "fastcc " : "ccc ");
            out += ret_type + " ";
            out += target + "* @"
                + seeker->second.versions[i].name + arguments;
            if(ret_type == "void")
              out += constant(get_unique_res("i1"),"i1","true");
            return out;
          }
        }
      }
      if(name == "print")
      {
        //This must be the ghost print function, because the protype didn't match any existing one
        return out + ghost_print(in);
      }
      /*else if(isInteger(name))
      {
        string type = "i"+width(name);
        out += get_unique_res(type) + " = bitcast " + latest_type() + " " + get_current_res() + " to " + type + "\n";
        cout << "EWFERWREGERE" << latest_type() << endl;
        return out;
      }*/
      else
      {
        //Name not found in the function table, now let's try variables
        for(long i = ScopeDepth; i != -1; i--)
        {
          map<string,Variable>::iterator seeker = master.SymbolTable[i].vars.find(name);
          if(seeker != master.SymbolTable[i].vars.end())
          {
            if(isFunctionPointer(seeker->second.type))
            {
              string target = cleanPointer(seeker->second.type);
              if((pointer == target) ||
                 (string(pointer,0,target.find("...")) == string(target,0,target.find("..."))))
              {
                //Found our match, emit code to call the pointer
                string ret_type = string(seeker->second.type,0,seeker->second.type.find('('));
                out += ((ret_type != "void") ? get_unique_res(ret_type) + " = ":"") + "call " + ret_type + " " + seeker->first + arguments;
                //If the function is an accessor, autoload the pointer
                if(ret_type == "void")
                  out += constant(get_unique_res("i1"),"i1","true");
                return out;
              }
            }
          }
        }
      }
      error(in,"No function (Or variable with a matching function pointer type) matches the name '",name,"' and the protype ",pointer,".");
    }
    else if(islist(car(in)))
    {
      //Emit code for the for the form. Is it a function pointer?
      out += emitCode(nth(in,0));
      if(isFunctionPointer(latest_type()))
      {
        string target = cleanPointer(latest_type());
        if((pointer == target) ||
           (string(pointer,0,target.find("...")) == string(target,0,target.find("..."))))
        {
          string ret_type = string(latest_type(),0,latest_type().find('('));
          out += ((ret_type != "void") ? get_unique_res(ret_type) + " = ":"") + "call " + ret_type + " " + pointer + "* " + get_current_res() + arguments;
          if(ret_type == "void")
              out += constant(get_unique_res("i1"),"i1","true");
          return out;
        }
      }
      else
        error(in,"Tried to compile the first element of the form '",in,"' to see if it was a callable function pointer, but an object of type '",latest_type(),"' was found.");
    }
    return out;
  }
}
