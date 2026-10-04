struct cline { char *line; int millis; };

template<class LINE> struct consolebuffer
{
    int maxlines;
    vector<LINE> conlines;

    consolebuffer(int maxlines = 100) : maxlines(maxlines) {}

    LINE &addline(const char *sf, int millis)        // add a line to the console buffer
    {
        LINE cl;
        cl.line = conlines.length()>maxlines ? conlines.pop().line : newstringbuf("");   // constrain the buffer size
        cl.millis = millis;                        // for how long to keep line on screen
        extern void encodeigraphs(char *d, const char *s, int len);
        encodeigraphs(cl.line, sf, MAXSTRLEN);
        return conlines.insert(0, cl);
    }

    void setmaxlines(int numlines)
    {
        maxlines = numlines;
        while(conlines.length() > maxlines) delete[] conlines.pop().line;
    }

    virtual ~consolebuffer()
    {
        while(conlines.length()) delete[] conlines.pop().line;
    }

    virtual void render() = 0;
};

struct textinputbuffer
{
    string buf;
    int pos, max;

    textinputbuffer() : pos(-1), max(0)
    {
        buf[0] = '\0';
    }

    int maxlen() const
    {
        return max ? max : sizeof(buf);
    }

    bool say(const char *c);    // returns true if buffer was modified

    void pasteclipboard();

    bool key(int code)          // returns true if buffer was modified
    {
        switch(code)
        {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                break;

            case SDLK_HOME:
                if(strlen(buf)) pos = 0;
                break;

            case SDLK_END:
                pos = -1;
                break;

            // pos < 0 means "at the end"; pos >= 0 is a byte offset that always
            // sits on a codepoint boundary, so multi-byte chars stay intact
            case SDLK_DELETE:
            {
                if(pos<0) break;
                int len = (int)strlen(buf);
                if(pos>=len) { pos = -1; break; }
                int next = utf8_next(buf, pos);
                memmove(&buf[pos], &buf[next], len - next + 1);
                if(pos >= len - (next - pos)) pos = -1;
                return true;
            }

            case SDLK_BACKSPACE:
            {
                int len = (int)strlen(buf), i = pos>=0 ? pos : len;
                if(i<1) break;
                int prev = utf8_prev(buf, i);
                memmove(&buf[prev], &buf[i], len - i + 1);
                if(pos>0) pos = prev;
                return true;
            }

            case SDLK_LEFT:
                if(pos > 0) pos = utf8_prev(buf, pos);
                else if(pos < 0 && buf[0]) pos = utf8_prev(buf, (int)strlen(buf));
                break;

            case SDLK_RIGHT:
            {
                if(pos<0) break;
                int next = utf8_next(buf, pos);
                pos = next >= (int)strlen(buf) ? -1 : next;
                break;
            }

            case SDLK_v:
                if(SDL_GetModState() & MOD_KEYS_CTRL)
                {
                    pasteclipboard();
                    return true;
                }
                break;

            default:
                break;
        }
        return false;
    }
};

