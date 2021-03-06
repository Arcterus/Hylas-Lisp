/*!
 * @file types.cpp
 * @brief Implementation of Hylas' type system.
 * @author Eudoxia
 */

namespace Hylas
{
  using namespace std;
  using namespace llvm;
  
  unsigned int width(string integer)
  {
    return from_string<int>(string(integer,1,integer.length()-1));
  }
  
  unsigned long typeSize(string type)
  {
    unsigned long size;
    if(type[type.length()-1] == '*')
      size = sizeof(size_t)*8; //The width of a pointer is the width of a machine word
    else if(isInteger(type))
    {
      size = width(type);
    }
    else if(type == "half")
    {
      size = 16;
    }
    else if(type == "float")
    {
      size = 32;
    }
    else if(type == "double")
    {
      size = 64;
    }
    else if(type == "x86_fp80")
    {
      size = 80;
    }
    else if(type == "fp128")
    {
      size = 128;
    }
    else if(type == "ppc_fp128")
    {
      size = 128;
    }     
    else if(type[0] == '{')
    {
      //Split into members, recursively size() and add them
      vector<string> members;
      string curr;
      type = cutfirst(cutlast(type));
      for(unsigned long i = 0; i < type.length(); i++)
      {
        if(type[i] == ',')
        {
          members.push_back(curr);
          curr.clear();
        }
        else
          curr += type[i];
      }
      for(unsigned long i = 0; i < members.size(); i++)
      {
        size += typeSize(members[i]);
      }
    }
    else
      nerror("Unknown type");
    return size;
  }
  
  bool isInteger(string in)
  {
    unsigned int bit_width = from_string<long>(string(in,1,in.length()-1));
    return ((in[0] == 'i') && ((bit_width >= 1) && (bit_width < (pow(2,23)-1))));
  }
  
  bool isPointer(string in)
  {
    if(in[in.length()-1] == '*')
      return true;
    return false;
  }
  
  bool isFunctionPointer(string in)
  {
    return ((in.find('(') != string::npos) &&
            (in.find(')') != string::npos) &&
            (in.find('(') < in.find(')')) &&
            (in[in.length()-1] == '*'));
    //TODO some other tests are needed
  }
  
  unsigned long countIndirection(string in)
  {
    unsigned long indirection = 0;
    for(unsigned long i = 0; i < in.length(); i++)
    {
      if(in[i] == '*')
        indirection++;
    }
    return indirection;
  }
  
  unsigned int fpwidth(string in)
  {
    //Doesn't return the width of a float, rather, returns its place in the
    //CoreTypes table, for <, >, <= etc. comparison
    for(unsigned long i = 0; i < CoreTypes.size(); i++)
    {
      if(CoreTypes[i] == in)
        return i;
    }
    return 0;
  }
  
  bool isCoreType(string in)
  {
    if(in == "...")
      return false;
    if(isInteger(in))
      return true;
    for(unsigned int i = 0; i < CoreTypes.size(); i++)
    {
      if(CoreTypes[i] == in)
        return true;
    }
    return false;
  }
  
  bool isBasicType(string in)
  {
    if(BasicTypes.count(in) > 0)
      return true;
    return false;
  }
  
  string specializeType(Generic* in, map<string,Form*> replacements, string signature)
  {
    master.errormode = GenericError;
    Form* specialization_code = editForm(in->code, replacements);
    string out = signature + " = type { ";
    string type;
    for(unsigned long i = 0; i < length(cdr(cdr(specialization_code))); i++)
    {
      type = printTypeSignature(cadr(nth(cdr(cdr(specialization_code)),i)));
      in->specializations[signature][val(car(nth(cdr(cdr(specialization_code)),i)))] = pair<int,string>(i,type);
      out += type + ",";
    }
    out = cutlast(out)+" }\n";
    //Specialize methods
    for(unsigned long i = 0; i < in->methods.size(); i++)
    {
      out += "\n" + emitCode(editForm(in->methods[i],replacements));
    }
    //Return the code for the specialization
    reseterror();
    return out;
  }
  
  string printTypeSignature(Form* form)
  {
    if(form == NULL)
    { Unwind(); }
    else if(isatom(form))
    {
      string tmp = val(form);
      if(isCoreType(tmp))
        return tmp;
      else
      {
        map<string,Type>::iterator seeker = BasicTypes.find(tmp);
        if(seeker != BasicTypes.end())
        {
          //push("%" + tmp + " = type " + seeker->second.definition);
          return "%" + tmp;
          //return seeker->second.definition;
        }
        else
          error(form,"Came across an unknown type: '",tmp,"'.");
      }
    }
    else if(islist(form))
    {
      if(islist(car(form)))
        error(form,"The name of a Generic type must be a symbolic atom, not a list.");
      string type_name = val(car(form));
      if(type_name == "pointer")
      {
        if(length(form) != 2)
          error(form,"(pointer) takes exactly one argument.");
        return printTypeSignature(cadr(form))+"*";
      }
      else if(type_name == "unpointer")
      {
        if(length(form) != 2)
          error(form,"(unpointer) takes exactly one argument.");
        string signature = printTypeSignature(cadr(form));
        if(signature[signature.length()-1] != '*')
          error(form,"Trying to (unpointer) a non-pointer type.");
        return cutlast(signature);
      }
      else if(type_name == "typeof")
      {
        if(length(form) != 2)
          error(form,"(typeof) takes exactly one argument.");
        emitCode(cadr(form));
        return latest_type();
      }
      for(unsigned long i = 0; i < Generics.size(); i++)
      {
        if((Generics[i].first == type_name) && (Generics[i].second.id == typeStructure))
        {
          if(Generics[i].second.arguments.size() != length(form)-1)
          {
            error(form,"Wrong number of arguments to specialize the Generic '",
                  type_name,"'.",to_string(Generics[i].second.arguments.size()),
                  " are required, but ",to_string<unsigned long>(length(form)-1)," were given.");
          }
          else
          {
            string signature = "%" + type_name + "._";
            unsigned long j = 1;
            for(; j < length(form); j++)
            {
              signature += printTypeSignature(nth(form,j)) + "_";
            }
            signature = cutlast(signature);
            for(map<string,map<string, pair<unsigned long,string> > >::iterator seeker = Generics[i].second.specializations.begin();
                seeker != Generics[i].second.specializations.end(); seeker++)
            { 
              if(seeker->first == signature)
                return signature;
            }
            map<string,Form*> replacements;
            for(j = 0; j < Generics[i].second.arguments.size(); j++)
            {
              replacements[Generics[i].second.arguments[j]] = nth(form,j+1);
            }
            for(j = 0; j < signature.length(); j++)
            {
              if(signature[j] == '*')
              {
                signature.replace(j,3,"ptr");
              }
            }
            signature = signature + "_.";
            push(specializeType(&(Generics[i].second),replacements,signature));
            return signature;
          }
        }
      }
      error(form,"An unknown Generic type, '",type_name,"' was provided for specialization.");
    }
    return "void";
  }

  Form* reverseTypeSignature(string signature)
  {
	return readString("null");
  }

  bool checkTypeExistence(string name)
  {
    return (isCoreType(name) || isBasicType(name));
  }
  
  MembersMap* findStructure(string in)
  {
    in = cutfirst(in);
    map<string,Type>::iterator seeker = BasicTypes.find(in);
    if(seeker != BasicTypes.end())
    {
      //Found matching type name, check if it's a structur
      if(seeker->second.id == typeStructure)
        return &(seeker->second.members);
      else
        return NULL;
    }
    //Find in Generics
    for(unsigned long i = 0; i < Generics.size(); i++)
    {
      map<string,map<string,pair<unsigned long,string> > >::iterator finder = Generics[i].second.specializations.find("%"+in);
      if(finder != Generics[i].second.specializations.end())
        return &(finder->second);
      if(i == Generics.size()-1)
        return NULL;
    }
    return NULL;
  }
  
  string assembleDefinition(MembersMap* members)
  {
    //cerr << "Assembling definition for struct with size " << members->size() << endl;
    string out = "{";
    for(MembersMap::iterator i = members->begin();
        i != members->end(); i++)
    {
      out += i->second.second + ",";
      //cerr << "Out so far: " << out;
    }
    return cutlast(out) + "}";
  }
  
  string makeType(Form* in)
  {
    Type tmp;
    tmp.id = typeSimple;
    if(length(in) != 3)
    {
      error(in,"(type) takes exactly 3 arguments. ",to_string(length(in)),
            " were given.");
    }
    if(isatom(cadr(in)))
    {
      if(analyze(val(cadr(in))) != Symbol)
      {
        printf("ERROR: Type name is a non-symbolic atom.");
        Unwind();
      }
    }
    string name = val(nth(in,1));
    if(checkTypeExistence(name))
      error(in,"Type '",name,"' already exists.");
    string type;
    if(isatom(nth(in,2)))
    {
      if(val(nth(in,2)) == "opaque")
      {
        type = "opaque"; //Opaque types
        tmp.definition = "opaque";
      }
    }
    type = printTypeSignature(nth(in,2));
    if(type != "opaque")
    {
      MembersMap* isstruct = findStructure(type);
      if(isstruct == NULL)
      {
        //cerr << "Not a structure" << endl;
        tmp.definition = type;
      }
      else
      {
        //cerr << "It's a structure" << endl;
        tmp.definition = assembleDefinition(isstruct);     
      }
    }
    BasicTypes[name] = tmp;
    persistentPush("%"+ name + " = type " + tmp.definition);
    push("%"+ name + " = type " + tmp.definition);
    return constant(get_unique_res("i1"),"i1","true");
  }

  void makePrintFunction(string name, unsigned long long nmembers)
  {
	string fn = "(function print (pointer i8) ((arg " + name + "))";
	fn += "\"le unfinished print function face :--DDD\")";
	emitCode(readString(fn));
  }

  void validateStructure(Form* in)
  {
    if(length(in) < 3)
    {
      printf("ERROR: Wrong number of arguments to (structure).");
      Unwind();
    }
    if(isatom(cadr(in)))
    {
      if(analyze(val(cadr(in))) != Symbol)
      {
        printf("ERROR: Structure name is a non-symbolic atom.");
        Unwind();
      }
    }
  }

  string makeStructure(Form* in)
  {
    Type tmp;
    tmp.id = typeStructure;
    validateStructure(in);
    string name = val(nth(in,1));
    if(checkTypeExistence(name))
      error(in,"Type '",name,"' already exists.");
    BasicTypes[name] = tmp;
    for(unsigned long i = 2; i < length(in); i++)
    {
      Form* current_field = nth(in,i);
      if(islist(current_field))
      {
        if(length(current_field) != 2)
        {
          printf("ERROR: Structure fields must either be a (name type) pair or a single symbol in case of generic structures");
          Unwind();
        }
        else if(islist(car(current_field)))
        {
          printf("ERROR: Can't use a list as a field name.");
          Unwind();
        }
        else
        {
          string field_name = val(car(current_field));
          if(analyze(field_name) != Symbol)
          {
            printf("ERROR: Can't use a non-symbolic atom as a field name.");
            Unwind();
          }
          tmp.members[field_name] = pair<long,string>(i-2,printTypeSignature(cadr(current_field)));
        }
      }
      else
      {
        printf("ERROR: Structure fields must be lists, not atoms.");
        Unwind();
      }
    }
    BasicTypes[name].members = tmp.members;
    string out = "%" + name + " = type ";
    string type = "{";
    for(map<string,pair<unsigned long,string> >::iterator seeker = tmp.members.begin(); seeker != tmp.members.end(); seeker++)
    {
      type += seeker->second.second + ",";
    }
    type = cutlast(type) + "}";
    BasicTypes[name].definition = type;
    persistentPush(out + BasicTypes[name].definition);
    push("%"+ name + " = type " + BasicTypes[name].definition);
	makePrintFunction(name,length(in)-1);
    return constant(get_unique_res("i1"),"i1","true");
  }
  
  bool checkGenericExistence(string name, bool id)
  {
    for(unsigned long i = 0; i < Generics.size(); i++)
    {
      if(Generics[i].first == name)
      {
        if(Generics[i].second.id == id)
          return true;
      }
    }
    return false;
  }
  
  Generic writeGeneric(Form* in, bool type)
  {
    //0        1                    2       3        4
    //(generic [structure|function] [name] ([args]) [code*]
    Generic out;
    out.id = type;
    if(type == typeStructure)
    {
      /* Now we modify the code to make it all ready for specialization
       * We star off with:
       * (generic structure derp (a) (element a))
       * And we end up with:
       * (structure derp (element a))
       */
      validateStructure(in);
      string name = val(nth(in,2));
      Form* code = readString("(structure " + name + ")");
      code = append(code, cdr(cdr(cdr(cdr(in)))));
      out.code = code;
      for(unsigned long i = 2; i < length(code); i++)
      {
        Form* current_field = nth(code,i);
        cout << "current field: " << current_field << endl;
        if(islist(current_field))
        {
          if(length(current_field) != 2)
          {
            printf("ERROR: Structure fields must either be a (name type) pair or a single symbol in case of generic structures");
            Unwind();
          }
          else if(islist(car(current_field)))
          {
            printf("ERROR: Can't use a list as a field name.");
            Unwind();
          }
          else
          {
            string field_name = val(car(current_field));
            if(analyze(field_name) != Symbol)
            {
              printf("ERROR: Can't use a non-symbolic atom as a field name.");
              Unwind();
            }
          }
        }
        else
        {
          printf("ERROR: Structure fields must be lists, not atoms.");
          Unwind();
        }
      }
    }
    else if(type == typeFunction)
    {
      /* Now we modify the code to make it all ready for specialization
       * We star off with:
       * (generic function durp (a b c)
       *   (function a ((i64 n) (i64 m)) ...))
       * And we end up with:
       * (function durp a ((i64 n) (i64 m)) ...)
       */
      string name = val(nth(in,2));
      Form* code = readString("(" + val(car(nth(in,4))) + " " + name + ")");
      code = append(code,cdr(nth(in,4)));
      out.code = code;
    }
    Form* arglist = nth(in,3);
    if(length(arglist) == 0)
    {
      printf("ERROR: Wrong number of arguments.");
      Unwind();
    }
    if(islist(arglist))
    {
      for(unsigned long i = 0; i < length(arglist); i++)
      {
        if(isatom(nth(arglist,i)))
        {
          if(analyze(val(nth(arglist,i))) != Symbol)
          {
            printf("ERROR: Arguments must be symbolic atoms.");
            Unwind();
          }
          else
          {
            out.arguments.push_back(val(nth(arglist,i)));
          }
        }
        else
        {
          printf("ERROR: Arguments must be symbolic atoms, but a list was found.");
          Unwind();
        }
      }
    }
    else
    {
      printf("ERROR: Argument list is not a list.");
      Unwind();
    }
    return out;
  }
  
  void addGeneric(string name, Generic in)
  {
    Generics.push_back(pair<string,Generic>(name,in));
  }
  
  Generic addGenericAttachment(string name, Form* in)
  {
    Generic out;
    for(unsigned long i = 0; i < Generics.size(); i++)
    {
      if(Generics[i].first == name)
      {
        if(Generics[i].second.id == typeStructure)
        {
          //Input: (generic method of [type]
          //    (function|recursive|macro|etc... [name] [ret]* (...args...) ...body...)
          //Output:  (function|recursive|macro|etc... [name] [ret*] ((self [type]) ...args...) ...body...)
          Form* code = readString("(" + val(car(nth(in,5))) + " " + val(nth(in,2)) + ")");
          code = append(code,cdr(nth(in,5)));
          Generics[i].second.methods.push_back(code);
          out = Generics[i].second;
          break;
        }
      }
    }
    return out;
  }
  
  Generic makeGeneric(Form* in)
  {
    Generic out;
    if(length(in) < 4)
      error(in,"Wrong number of arguments.");
    if(isatom(nth(in,1)))
    {
      string text = val(nth(in,1));
      if(analyze(text) == Symbol)
      {
        Generic out;
        string name = val(nth(in,2));
        if(analyze(name) != Symbol)
        {
          printf("ERROR: The name of the generic must be a symbolic atom.");
          Unwind();
        }
        if(text == "structure")
        {
          out = writeGeneric(in,typeStructure);
          if(checkGenericExistence(name,typeStructure))
          {
            printf("ERROR: Generic already exists.");
            Unwind();
          }
          addGeneric(name,out);
        }
        else if(text == "function")
        {
          out = writeGeneric(in,typeFunction);
          if(checkGenericExistence(name,typeFunction))
          {
            printf("ERROR: Generic already exists.");
            Unwind();
          }
          addGeneric(name,out);
        }
        else if(text == "method")
        {
          if(isatom(nth(in,2)))
          {
            if(val(nth(in,2)) == "of")
            {
              if(isatom(nth(in,3)))
              {
                if(analyze(val(nth(in,3))) == Symbol)
                {
                  string generic_name = val(nth(in,3));
                  if(!checkGenericExistence(generic_name,typeStructure))
                  {
                    printf("ERROR: The Generic for this method doesn't exist.");
                    Unwind();
                  }
                  else
                  {
                    if(text == "method")
                      out = addGenericAttachment(generic_name,in);
                    return out;
                  }
                }
              }
              error(in,"The third argument to (generic) must be a symbolic atom.");
            }
          }
          error(in,"The second argument to (generic) must be 'of'.");
        }
      }
    }
    else
      error(in,"ERROR: The second argument to (generic) must be 'function', 'structure', 'method' or 'macro'.");
    return out;
  }
  
  string genericInterface(Form* form)
  {
    Generic tmp = makeGeneric(form);
    if(val(nth(form,1)) != "method")
      Generics.push_back(pair<string,Generic>(val(nth(form,2)),tmp));
    return "";
  }
  
  string ghost_print(Form* form)
  {
    string message, out;
    bool ptr = false;
    if(isFunctionPointer(latest_type()))
    {
      message = "<0x%X pointer to function " + cutlast(latest_type()) + " with indirection " + to_string(countIndirection(latest_type())) + ">";
      ptr = true;
    }
    else if(isPointer(latest_type()))
    {
      message = "<0x%X pointer to " + cutfirst(cutlast(latest_type())) + " with indirection " + to_string(countIndirection(latest_type())) + ">";
      ptr = true;
    }
    else if(isInteger(latest_type()))
    {
      message = "<" + latest_type() + " unprintable integer (too wide)>";
    }
    else if(isCoreType(latest_type()))
    {
      message = "<" + latest_type() + " unprintable floating point>";
    }
    else
    {
      warn(form,"Execution was carried out, but I don't even know what to do with this type (",latest_type(),").");
    }
    out += emitCode(readString("\""+message+"\""));
    if(ptr)
    {
      out += gensym() + " = call i32 (i8*,...)* @printf(i8* " + get_current_res() 
          + ", " + res_type(get_res(res_version-1)) + " " + get_res(res_version-1) + ")\n";
	  //out += constant(get_unique_res("i1"),"i1","true");
	  /*unsigned long message_addr;
	  out += get_unique_res("i8*") + " = call i8* @malloc(" + mword + " 400)\n";
      out += get_unique_res("i32") + " = call i32 (i8*,i8*,...)* @sprintf(i8* " + get_current_res() + ", i8* " + get_res(message_addr) 
          + ", " + res_type(get_res(res_version-2)) + " " + get_res(res_version-2) + ")\n";
	  out += get_unique_res("i8*") + " = call i8* @malloc(" + mword + " " + get_current_res() + ")\n";
	  out += "call i32 (i8*,i8*,...)* @sprintf(i8* " + get_current_res() + ", i8* " + get_res(message_addr) + ", i32* " + get_res(message_addr-1) + ")\n";
	  */
      //FIXME: Change this to sprintf etc.
    }
    return out;
  }
  
  void init_types()
  {
    CoreTypes.push_back("half");
    CoreTypes.push_back("float");
    CoreTypes.push_back("double");
    CoreTypes.push_back("x86_fp80");
    CoreTypes.push_back("fp128");
    CoreTypes.push_back("ppc_fp128");
    CoreTypes.push_back("...");
    //CoreTypes = {"half", "float", "double", "x86_fp80", "fp128", "ppc_fp128", "..." };
  }
}
