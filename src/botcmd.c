/*
 * botcmd.c -- handles:
 *   commands that comes across the botnet
 *   userfile transfer and update commands from sharebots
 *
 * $Id: botcmd.c,v 1.2.2.1 2010/11/10 13:39:19 pseudo Exp $
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999 - 2010 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "main.h"
#include "tandem.h"
#include "users.h"
#include "chan.h"
#include "modules.h"

extern char botnetnick[], ver[], admin[], network[], motdfile[];
extern int dcc_total, remote_boots, noshare;
#ifdef TLS
extern int tls_vfybots;
extern int tls_vfyclients;
extern SSL_CTX *ssl_ctx;
#endif
extern struct dcc_t *dcc;
extern struct chanset_t *chanset;
extern struct userrec *userlist;
extern Tcl_Interp *interp;
extern time_t now, online_since;
extern party_t *party;
extern module_entry *module_list;

static char TBUF[1024]; /* Static buffer for goofy bot stuff */

static char base64to[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0,
  0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 62, 0, 63, 0, 0, 0, 26, 27, 28,
  29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
  49, 50, 51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};


int base64_to_int(char *buf)
{
  int i = 0;

  while (*buf) {
    i = i << 6;
    i += base64to[(int) *buf];
    buf++;
  }
  return i;
}

/* Used for 1.0 compatibility: if a join message arrives with no sock#,
 * i'll just grab the next "fakesock" # (incrementing to assure uniqueness)
 */
static int fakesock = 2300;

static void fake_alert(int idx, char *item, char *extra)
{
  static unsigned long lastfake; /* The last time fake_alert was used */

  if (now - lastfake > 10) {
#ifndef NO_OLD_BOTNET
    if (b_numver(idx) < NEAT_BOTNET)
      dprintf(idx, "chat %s NOTICE: %s (%s != %s).\n",
              botnetnick, _("Fake message rejected"), item, extra);
    else
#endif
      dprintf(idx, "ct %s NOTICE: %s (%s != %s).\n",
              botnetnick, _("Fake message rejected"), item, extra);
    putlog(LOG_BOTS, "*", _("%s Fake message rejected (%s != %s)."),
           dcc[idx].nick, item, extra);
    lastfake = now;
  }
}

/* chan <from> <chan> <text>
 */
static void bot_chan2(int idx, char *msg)
{
  char *from, *p, *s;
  int i, chan;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  from = newsplit(&msg);
  p = newsplit(&msg);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    chan = atoi(p);
  else
#endif
    chan = base64_to_int(p);
  /* Strip annoying control chars */
  for (p = s = from; *s; s++)
    if ((*s > 31) && (*s != 127))
      *p++ = *s;
  if (*p)
    *p = 0;
  p = strchr(from, '@');
  if (p) {
    snprintf(TBUF, sizeof(TBUF), "<%s> %s", from, msg);
    *p = 0;
    if (!partyidle(p + 1, from)) {
      *p = '@';
      fake_alert(idx, "user", from);
      return;
    }
    *p = '@';
    p++;
  } else {
    sprintf(TBUF, "*** (%s) %s", from, msg);
    p = from;
  }
  i = nextbot(p);
  if (i != idx) {
    fake_alert(idx, "direction", p);
  } else {
    chanout_but(-1, chan, "%s\n", TBUF);
    /* Send to new version bots */
    if (i >= 0)
      botnet_send_chan(idx, from, NULL, chan, msg);
    if (strchr(from, '@') != NULL)
      check_tcl_chat(from, chan, msg);
    else
      check_tcl_bcst(from, chan, msg);
  }
}

/* chat <from> <notice>  -- only from bots
 */
static void bot_chat(int idx, char *par)
{
  char *from;
  int i;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  from = newsplit(&par);
  if (strchr(from, '@') != NULL) {
    fake_alert(idx, "bot", from);
    return;
  }
  /* Make sure the bot is valid */
  i = nextbot(from);
  if (i != idx) {
    fake_alert(idx, "direction", from);
    return;
  }
  chatout("*** (%s) %s\n", from, par);
  botnet_send_chat(idx, from, par);
  check_tcl_bcst(from, -1, par);
}

/* actchan <from> <chan> <text>
 */
static void bot_actchan(int idx, char *par)
{
  char *from, *p, *s;
  int i, chan;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  from = newsplit(&par);
  p = strchr(from, '@');
  if (p == NULL) {
    /* How can a bot do an action? */
    fake_alert(idx, "user@bot", from);
    return;
  }
  *p = 0;
  if (!partyidle(p + 1, from)) {
    *p = '@';
    fake_alert(idx, "user", from);
    return;
  }
  *p = '@';
  p++;
  i = nextbot(p);
  if (i != idx) {
    fake_alert(idx, "direction", p);
    return;
  }
  p = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    chan = atoi(p);
  else
#endif
    chan = base64_to_int(p);
  for (p = s = from; *s; s++)
    if ((*s > 31) && (*s != 127))
      *p++ = *s;
  if (*p)
    *p = 0;
  chanout_but(-1, chan, "* %s %s\n", from, par);
  botnet_send_act(idx, from, NULL, chan, par);
  check_tcl_act(from, chan, par);
}

/* priv <from> <to> <message>
 */
static void bot_priv(int idx, char *par)
{
  char *from, *p, *to = TBUF, *tobot;
  int i;

  from = newsplit(&par);
  tobot = newsplit(&par);
  splitc(to, tobot, '@');
  p = strchr(from, '@');
  if (p != NULL)
    p++;
  else
    p = from;
  i = nextbot(p);
  if (i != idx) {
    fake_alert(idx, "direction", p);
    return;
  }
  if (!to[0])
    return; /* Silently ignore notes to '@bot' */
  if (!egg_strcasecmp(tobot, botnetnick)) { /* For me! */
    if (p == from)
      add_note(to, from, par, -2, 0);
    else {
      i = add_note(to, from, par, -1, 0);
      if (from[0] != '@')
        switch (i) {
        case NOTE_ERROR:
          botnet_send_priv(idx, botnetnick, from, NULL,
                           _("No such user %s."), to);
          break;
        case NOTE_STORED:
          botnet_send_priv(idx, botnetnick, from, NULL,
                           _("Not online; note stored."));
          break;
        case NOTE_FULL:
          botnet_send_priv(idx, botnetnick, from, NULL,
                           _("Notebox is full, sorry."));
          break;
        case NOTE_AWAY:
          botnet_send_priv(idx, botnetnick, from, NULL,
                           _("%s is away; note stored."), to); 
          break;
        case NOTE_FWD:
          botnet_send_priv(idx, botnetnick, from, NULL,
                           _("Not online; note forwarded to: %s"), to);
          break;
        case NOTE_REJECT:
          botnet_send_priv(idx, botnetnick, from, NULL,
                           _("%s rejected your note."), to);
          break;
        case NOTE_TCL:
          break;                /* Do nothing */
        case NOTE_OK:
          botnet_send_priv(idx, botnetnick, from, NULL,
                           _("Note sent to %s."), to);
          break;
        }
    }
  } else {                        /* Pass it on */
    i = nextbot(tobot);
    if (i >= 0)
      botnet_send_priv(i, from, to, tobot, "%s", par);
  }
}

static void bot_bye(int idx, char *par)
{
  char s[1024];
  int users, bots;

  bots = bots_in_subtree(findbot(dcc[idx].nick));
  users = users_in_subtree(findbot(dcc[idx].nick));
  simple_sprintf(s, _("Disconnected from: %s. %s (lost %d %s and %d %s)"),
                 dcc[idx].nick, par[0] ? par : "No reason",
                 bots, P_("bot", "bots", bots), 
                 users, P_("user", "users", users));
  putlog(LOG_BOTS, "*", "%s", s);
  chatout("*** %s\n", s);
  botnet_send_unlinked(idx, dcc[idx].nick, s);
  dprintf(idx, "*bye\n");
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void remote_tell_who(int idx, char *nick, int chan)
{
  int i = 10, k, l, ok = 0;
  char s[1024], *realnick;
  struct chanset_t *c;

  realnick = strchr(nick, ':');
  if (realnick)
    realnick++;
  else
    realnick = nick;
  putlog(LOG_BOTS, "*", "#%s# who", realnick);
  strcpy(s, _("Channels: "));
  for (c = chanset; c; c = c->next)
    if (!channel_secret(c) && !channel_inactive(c)) {
      l = strlen(c->dname);
      if (i + l < 1021) {
        if (i > 10)
          sprintf(s, "%s, %s", s, c->dname);
        else {
          strcpy(s, c->dname);
          i += (l + 2);
        }
      }
    }
  if (i > 10) {
    botnet_send_priv(idx, botnetnick, nick, NULL, "%s (%s)", s, ver);
  } else
    botnet_send_priv(idx, botnetnick, nick, NULL, _("no channels (%s)"), ver);
  if (admin[0])
    botnet_send_priv(idx, botnetnick, nick, NULL, _("Admin: %s"), admin);
  if (chan == 0)
    botnet_send_priv(idx, botnetnick, nick, NULL, _("Party line members: "
                     "(* = owner, + = master, %% = botmaster, "
                     "@ = op, ^ = halfop)"));
  else {
    simple_sprintf(s, "assoc %d", chan);
    if ((Tcl_Eval(interp, s) != TCL_OK) || tcl_resultempty())
      botnet_send_priv(idx, botnetnick, nick, NULL, _("People on channel %s%d: "
                       "(* = owner, + = master, %% = botmaster, @ = op, "
                       "^ = halfop)\n"), (chan < GLOBAL_CHANS) ? "" : "*",
                       chan % GLOBAL_CHANS);
    else
      botnet_send_priv(idx, botnetnick, nick, NULL, _("People on channel '%s' "
                       "(%s%d): (* = owner, + = master, %% = botmaster, "
                       "@ = op, ^ = halfop)\n"), tcl_resultstring(),
                       (chan < GLOBAL_CHANS) ? "" : "*", chan % GLOBAL_CHANS);
  }
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type->flags & DCT_REMOTEWHO)
      if (dcc[i].u.chat->channel == chan) {
        k = sprintf(s, "  %c%-15s %s", (geticon(i) == '-' ? ' ' : geticon(i)),
                    dcc[i].nick, dcc[i].host);
        if (now - dcc[i].timeval > 300) {
          unsigned long days, hrs, mins;

          days = (now - dcc[i].timeval) / 86400;
          hrs = ((now - dcc[i].timeval) - (days * 86400)) / 3600;
          mins = ((now - dcc[i].timeval) - (hrs * 3600)) / 60;
          if (days > 0)
            sprintf(s + k, _(" (idle %lud%luh)"), days, hrs);
          else if (hrs > 0)
            sprintf(s + k, _(" (idle %luh%lum)"), hrs, mins);
          else
            sprintf(s + k, _(" (idle %lum)"), mins);
        }
        botnet_send_priv(idx, botnetnick, nick, NULL, "%s", s);
        if (dcc[i].u.chat->away != NULL)
          botnet_send_priv(idx, botnetnick, nick, NULL, _("      AWAY: %s"),
                           dcc[i].u.chat->away);
      }
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type == &DCC_BOT) {
      if (!ok) {
        ok = 1;
        botnet_send_priv(idx, botnetnick, nick, NULL, _("Bots connected:"));
      }
      sprintf(s, "  %s%c%-15s %s",
              dcc[i].status & STAT_CALLED ? "<-" : "->",
              dcc[i].status & STAT_SHARE ? '+' : ' ',
              dcc[i].nick, dcc[i].u.bot->version);
      botnet_send_priv(idx, botnetnick, nick, NULL, "%s", s);
    }
  ok = 0;
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type->flags & DCT_REMOTEWHO)
      if (dcc[i].u.chat->channel != chan) {
        if (!ok) {
          ok = 1;
          botnet_send_priv(idx, botnetnick, nick, NULL,
                           _("Other people on the bot:"));
        }
        l = sprintf(s, "  %c%-15s %s", (geticon(i) == '-' ? ' ' : geticon(i)),
                    dcc[i].nick, dcc[i].host);
        if (now - dcc[i].timeval > 300) {
          k = (now - dcc[i].timeval) / 60;
          if (k < 60)
            sprintf(s + l, _(" (idle %dm)"), k);
          else
            sprintf(s + l, _(" (idle %dh%dm)"), k / 60, k % 60);
        }
        botnet_send_priv(idx, botnetnick, nick, NULL, "%s", s);
        if (dcc[i].u.chat->away != NULL)
          botnet_send_priv(idx, botnetnick, nick, NULL,
                           _("      AWAY: %s"), dcc[i].u.chat->away);
      }
}

/* who <from@bot> <tobot> <chan#>
 */
static void bot_who(int idx, char *par)
{
  char *from, *to, *p;
  int i, chan;

  from = newsplit(&par);
  p = strchr(from, '@');
  if (!p) {
    sprintf(TBUF, "%s@%s", from, dcc[idx].nick);
    from = TBUF;
  }
  to = newsplit(&par);
  if (!egg_strcasecmp(to, botnetnick))
    to[0] = 0;
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    chan = atoi(par);
  else
#endif
    chan = base64_to_int(par);
  if (to[0]) {
    i = nextbot(to);
    if (i >= 0)
      botnet_send_who(i, from, to, chan);
  } else
    remote_tell_who(idx, from, chan);
}

static void bot_endlink(int idx, char *par)
{
  dcc[idx].status &= ~STAT_LINKING;
}

/* info? <from@bot>   -> send priv
 */
static void bot_infoq(int idx, char *par)
{
  char s[200], s2[32], *realnick;
  struct chanset_t *chan;
  time_t now2;
  int hr, min;

  /* Strip the idx from user@bot */
  realnick = strchr(par, ':');
  if (realnick)
    realnick++;
  else
    realnick = par;
  putlog(LOG_BOTS, "*", "#%s# botinfo", realnick);

  now2 = now - online_since;
  s2[0] = 0;
  if (now2 > 86400) {
    int days = now2 / 86400;

    /* Days */
    sprintf(s2, "%d %s,", days, P_("day", "days", days));
    now2 -= days * 86400;
  }
  hr = (time_t) ((int) now2 / 3600);
  now2 -= (hr * 3600);
  min = (time_t) ((int) now2 / 60);
  sprintf(&s2[strlen(s2)], "%02d:%02d", (int) hr, (int) min);
  if (module_find("server", 0, 0)) {
    s[0] = 0;
    for (chan = chanset; chan; chan = chan->next) {
      if (!channel_secret(chan)) {
        if ((strlen(s) + strlen(chan->dname) + strlen(network)
             + strlen(botnetnick) + strlen(ver) + 1) >= 200) {
          strcat(s, "++  ");
          break;                /* Yegads..! */
        }
        strcat(s, chan->dname);
        strcat(s, ", ");
      }
    }
    if (s[0]) {
      s[strlen(s) - 2] = 0;
      botnet_send_priv(idx, botnetnick, par, NULL,
                       _("%s <%s> (%s) [UP %s]"), ver, network, s, s2);
    } else
      botnet_send_priv(idx, botnetnick, par, NULL, _("%s <%s> (no channels) "
                      "[UP %s]"), ver, network, s2);
  } else
    botnet_send_priv(idx, botnetnick, par, NULL,
                     _("%s <NO_IRC> [UP %s]"), ver, s2);
  botnet_send_infoq(idx, par);
}

static void bot_ping(int idx, char *par)
{
  botnet_send_pong(idx);
}

static void bot_pong(int idx, char *par)
{
  dcc[idx].status &= ~STAT_PINGED;
}

/* link <from@bot> <who> <to-whom>
 */
static void bot_link(int idx, char *par)
{
  char *from, *bot, *rfrom;
  int i;

  from = newsplit(&par);
  bot = newsplit(&par);

  if (!egg_strcasecmp(bot, botnetnick)) {
    if ((rfrom = strchr(from, ':')))
      rfrom++;
    else
      rfrom = from;
    putlog(LOG_CMDS, "*", "#%s# link %s", rfrom, par);
    if (botlink(from, -1, par))
      botnet_send_priv(idx, botnetnick, from, NULL,
                       _("Attempting to link %s ..."), par);
    else
      botnet_send_priv(idx, botnetnick, from, NULL, _("Can't link there."));
  } else {
    i = nextbot(bot);
    if (i >= 0)
      botnet_send_link(i, from, bot, par);
  }
}

/* unlink <from@bot> <linking-bot> <undesired-bot> <reason>
 */
static void bot_unlink(int idx, char *par)
{
  char *from, *bot, *rfrom, *p, *undes;
  int i;

  from = newsplit(&par);
  bot = newsplit(&par);
  undes = newsplit(&par);
  if (!egg_strcasecmp(bot, botnetnick)) {
    if ((rfrom = strchr(from, ':')))
      rfrom++;
    else
      rfrom = from;
    putlog(LOG_CMDS, "*", "#%s# unlink %s (%s)", rfrom, undes, par[0] ? par :
           "No reason");
    i = botunlink(-3, undes, par[0] ? par : NULL, rfrom);
    if (i == 1) {
      p = strchr(from, '@');
      if (p) {
        /* idx will change after unlink -- get new idx
         *
         * TODO: This has changed with the new lostdcc() behaviour. Check
         *       if we can optimise the situation.
         */
        i = nextbot(p + 1);
        if (i >= 0)
          botnet_send_priv(i, botnetnick, from, NULL,
                           "Unlinked from %s.", undes);
      }
    } else if (i == 0) {
      botnet_send_unlinked(-1, undes, "");
      p = strchr(from, '@');
      if (p) {
        /* Ditto above, about idx */
        i = nextbot(p + 1);
        if (i >= 0)
          botnet_send_priv(i, botnetnick, from, NULL,
                           _("Can't unlink %s."), undes);
      }
    } else {
      p = strchr(from, '@');
      if (p) {
        i = nextbot(p + 1);
        if (i >= 0)
          botnet_send_priv(i, botnetnick, from, NULL,
                           _("Can't remotely unlink sharebots."));
      }
    }
  } else {
    i = nextbot(bot);
    if (i >= 0)
      botnet_send_unlink(i, from, bot, undes, par);
  }
}

/* Bot next share?
 */
static void bot_update(int idx, char *par)
{
  char *bot, x;
  int vnum;

  bot = newsplit(&par);
  x = par[0];
  if (x)
    par++;
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    vnum = atoi(par);
  else
#endif
    vnum = base64_to_int(par);
  if (in_chain(bot))
    updatebot(idx, bot, x, vnum);
}

/* Newbot next share?
 */
static void bot_nlinked(int idx, char *par)
{
  char *newbot, *next, *p, s[1024], x;
  int bogus = 0, i;
  struct userrec *u;

  newbot = newsplit(&par);
  next = newsplit(&par);
  s[0] = 0;
  if (!next[0]) {
    putlog(LOG_BOTS, "*", _("Invalid eggnet protocol from %s (zapfing)"),
           dcc[idx].nick);
    simple_sprintf(s, _("Disconnected %s (invalid bot)"), dcc[idx].nick);
    dprintf(idx, "error invalid eggnet protocol for 'nlinked'\n");
  } else if ((in_chain(newbot)) || (!egg_strcasecmp(newbot, botnetnick))) {
    /* Loop! */
    putlog(LOG_BOTS, "*", _("Loop detected %s (mutual: %s)"),
           dcc[idx].nick, newbot);
    simple_sprintf(s, _("Detected loop: two bots exist named %s: "
                   "disconnecting %s"), newbot, dcc[idx].nick);
    dprintf(idx, "error Loop (%s)\n", newbot);
  }
  if (!s[0]) {
    for (p = newbot; *p; p++)
      if ((*p < 32) || (*p == 127) || ((p - newbot) >= HANDLEN))
        bogus = 1;
    i = nextbot(next);
    if (i != idx)
      bogus = 1;
  }
  if (bogus) {
    putlog(LOG_BOTS, "*", _("Bogus link notice from %s!  (%s -> %s)"),
           dcc[idx].nick, next, newbot);
    simple_sprintf(s, _("Bogus link notice from: %s. Disconnected"),
                   dcc[idx].nick);
    dprintf(idx, "error %s (%s -> %s)\n", _("Bogus link notice from"),
            next, newbot);
  }
  if (bot_flags(dcc[idx].user) & BOT_LEAF) {
    putlog(LOG_BOTS, "*", _("Disconnected left %s  (Linked to %s)"),
           dcc[idx].nick, newbot);
    simple_sprintf(s, _("Illegal link by leaf %s (to %s): Disconnected"),
                   dcc[idx].nick, newbot);
    dprintf(idx, "error %s\n", _("You are supposed to be a leaf!"));
  }
  if (s[0]) {
    chatout("*** %s\n", s);
    botnet_send_unlinked(idx, dcc[idx].nick, s);
    dprintf(idx, "bye %s\n", _("Illegal link by leaf"));
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  x = par[0];
  if (x)
    par++;
  else
    x = '-';
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    i = atoi(par);
  else
#endif
    i = base64_to_int(par);
  botnet_send_nlinked(idx, newbot, next, x, i);
  if (x == '!') {
    chatout(_("*** (%s) Linked to %s.\n"), next, newbot);
    x = '-';
  }
  addbot(newbot, dcc[idx].nick, next, x, i);
  check_tcl_link(newbot, next);
  u = get_user_by_handle(userlist, newbot);
  if (bot_flags(u) & BOT_REJECT) {
    botnet_send_reject(idx, botnetnick, NULL, newbot, NULL, NULL);
    putlog(LOG_BOTS, "*", _("Rejecting bot %s from %s"), newbot,
           dcc[idx].nick);
  }
}

#ifndef NO_OLD_BOTNET
static void bot_linked(int idx, char *par)
{
  char s[1024];
  int bots, users;

  bots = bots_in_subtree(findbot(dcc[idx].nick));
  users = users_in_subtree(findbot(dcc[idx].nick));
  putlog(LOG_BOTS, "*", _("Older bot detected (unsupported)"));
  simple_sprintf(s, _("Disconnected %s (outdated) (lost %d %s and %d %s"),
                 dcc[idx].nick, bots, P_("bot", "bots", bots),
                 users, P_("user", "users", users));
  chatout("*** %s\n", s);
  botnet_send_unlinked(idx, dcc[idx].nick, s);
  killsock(dcc[idx].sock);
  lostdcc(idx);
}
#endif /* !NO_OLD_BOTNET */

static void bot_unlinked(int idx, char *par)
{
  int i;
  char *bot;

  bot = newsplit(&par);
  i = nextbot(bot);
  if ((i >= 0) && (i != idx))   /* Bot is NOT downstream along idx, so
                                 * BOGUS! */
    fake_alert(idx, "direction", bot);
  else if (i >= 0) {            /* Valid bot downstream of idx */
    if (par[0])
      chatout("*** (%s) %s\n", lastbot(bot), par);
    botnet_send_unlinked(idx, bot, par);
    unvia(idx, findbot(bot));
    rembot(bot);
  }
  /* Otherwise it's not even a valid bot, so just ignore! */
}

/* trace <from@bot> <dest> <chain:chain..>
 */
static void bot_trace(int idx, char *par)
{
  char *from, *dest;
  int i;

  from = newsplit(&par);
  dest = newsplit(&par);
  simple_sprintf(TBUF, "%s:%s", par, botnetnick);
  botnet_send_traced(idx, from, TBUF);
  if (egg_strcasecmp(dest, botnetnick) && ((i = nextbot(dest)) > 0))
    botnet_send_trace(i, from, dest, par);
}

/* traced <to@bot> <chain:chain..>
 */
static void bot_traced(int idx, char *par)
{
  char *to, *p;
  int i, sock;

  to = newsplit(&par);
  p = strchr(to, '@');
  if (p == NULL)
    p = to;
  else {
    *p = 0;
    p++;
  }
  if (!egg_strcasecmp(p, botnetnick)) {
    time_t t = 0;
    char *p = par, *ss = TBUF;

    splitc(ss, to, ':');
    if (ss[0])
      sock = atoi(ss);
    else
      sock = -1;
    if (par[0] == ':') {
      t = atoi(par + 1);
      p = strchr(par + 1, ':');
      if (p)
        p++;
      else
        p = par + 1;
    }
    for (i = 0; i < dcc_total; i++)
      if ((dcc[i].type->flags & DCT_CHAT) &&
          (!egg_strcasecmp(dcc[i].nick, to)) &&
          ((sock == -1) || (sock == dcc[i].sock))) {
        if (t) {
          int j = 0;

          {
            register char *c = p;

            for (; *c != '\0'; c++)
              if (*c == ':')
                j++;
          }
          dprintf(i, _("Trace result -> %s (%lu secs, %d %s)\n"), p, now - t,
                  P_("sec", "secs", now - t), j, P_("hop", "hops", j));
        } else
          dprintf(i, _("Trace result -> %s\n"), p);
      }
  } else {
    i = nextbot(p);
    if (p != to)
      *--p = '@';
    if (i >= 0)
      botnet_send_traced(i, to, par);
  }
}

/* reject <from> <bot>
 */
static void bot_reject(int idx, char *par)
{
  char *from, *who, *destbot, *frombot;
  struct userrec *u;
  int i;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  from = newsplit(&par);
  frombot = strchr(from, '@');
  if (frombot)
    frombot++;
  else
    frombot = from;
  i = nextbot(frombot);
  if (i != idx) {
    fake_alert(idx, "direction", frombot);
    return;
  }
  who = newsplit(&par);
  if (!(destbot = strchr(who, '@'))) {
    /* Rejecting a bot */
    i = nextbot(who);
    if (i < 0) {
      botnet_send_priv(idx, botnetnick, from, NULL,
                       _("Can't unlink %s (doesn't exist)"), who);
    } else if (!egg_strcasecmp(dcc[i].nick, who)) {
      char s[1024];

      /* I'm the connection to the rejected bot */
      putlog(LOG_BOTS, "*", _("%s rejected %s"), from, dcc[i].nick);
      dprintf(i, "bye %s\n", par[0] ? par : "rejected");
      if (par[0])
        simple_sprintf(s, _("Disconnected %s (%s: %s)"), dcc[i].nick,
                       from, par);
      else
        simple_sprintf(s, _("Disconnected %s (%s: rejected)"),
                       dcc[i].nick, from);
      chatout("*** %s\n", s);
      botnet_send_unlinked(i, dcc[i].nick, s);
      killsock(dcc[i].sock);
      lostdcc(i);
    } else {
      if (i >= 0)
        botnet_send_reject(i, from, NULL, who, NULL, par);
    }
  } else {                        /* Rejecting user */
    *destbot++ = 0;
    if (!egg_strcasecmp(destbot, botnetnick)) {
      /* Kick someone here! */
      int ok = 0;

      if (remote_boots == 1) {
        frombot = strchr(from, '@');
        if (frombot == NULL)
          frombot = from;
        else
          frombot++;
        u = get_user_by_handle(userlist, frombot);
        if (!(bot_flags(u) & BOT_SHARE)) {
          add_note(from, botnetnick, _("No non sharebot boots."), -1, 0);
          ok = 1;
        }
      } else if (remote_boots == 0) {
        botnet_send_priv(idx, botnetnick, from, NULL,
                         _("Remote boots are not allowed."));
        ok = 1;
      }
      for (i = 0; (i < dcc_total) && (!ok); i++)
        if ((!egg_strcasecmp(who, dcc[i].nick)) &&
            (dcc[i].type->flags & DCT_CHAT)) {
          u = get_user_by_handle(userlist, dcc[i].nick);
          if (u && (u->flags & USER_OWNER)) {
            add_note(from, botnetnick, _("Can't boot the bot owner."), -1, 0);
            return;
          }
          do_boot(i, from, par);
          ok = 1;
          putlog(LOG_CMDS, "*", "#%s# boot %s (%s)", from, who,
                 par[0] ? par : _("No reason"));
        }
    } else {
      i = nextbot(destbot);
      *--destbot = '@';
      if (i >= 0)
        botnet_send_reject(i, from, NULL, who, NULL, par);
    }
  }
}

static void bot_thisbot(int idx, char *par)
{
  if (egg_strcasecmp(par, dcc[idx].nick)) {
    char s[1024];

    putlog(LOG_BOTS, "*", _("Wrong bot--wanted %s,  got %s"), dcc[idx].nick, par);
    dprintf(idx, "bye %s\n", _("imposter"));
    simple_sprintf(s, _("Disconnected %s (imposter)"), dcc[idx].nick);
    chatout("*** %s\n", s);
    botnet_send_unlinked(idx, dcc[idx].nick, s);
    unvia(idx, findbot(dcc[idx].nick));
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return;
  }
  if (bot_flags(dcc[idx].user) & BOT_LEAF)
    dcc[idx].status |= STAT_LEAF;
  /* Set capitalization the way they want it */
  noshare = 1;
  change_handle(dcc[idx].user, par);
  noshare = 0;
  strcpy(dcc[idx].nick, par);
}

static void bot_handshake(int idx, char *par)
{
  struct userrec *u = get_user_by_handle(userlist, dcc[idx].nick);

  /* We *don't* want botnet passwords migrating */
  noshare = 1;
  set_user(&USERENTRY_PASS, u, par);
  noshare = 0;
}

/* Used to send a direct msg from Tcl on one bot to Tcl on another
 * zapf <frombot> <tobot> <code [param]>
 */
static void bot_zapf(int idx, char *par)
{
  char *from, *to;
  int i;

  from = newsplit(&par);
  to = newsplit(&par);
  i = nextbot(from);
  if (i != idx) {
    fake_alert(idx, "direction", from);
    return;
  }
  if (!egg_strcasecmp(to, botnetnick)) {
    /* For me! */
    char *opcode;

    opcode = newsplit(&par);
    check_tcl_bot(from, opcode, par);
    return;
  }
  i = nextbot(to);
  if (i >= 0)
    botnet_send_zapf(i, from, to, par);
}

/* Used to send a global msg from Tcl on one bot to every other bot
 * zapf-broad <frombot> <code [param]>
 */
static void bot_zapfbroad(int idx, char *par)
{
  char *from, *opcode;
  int i;

  from = newsplit(&par);
  opcode = newsplit(&par);

  i = nextbot(from);
  if (i != idx) {
    fake_alert(idx, "direction", from);
    return;
  }
  check_tcl_bot(from, opcode, par);
  botnet_send_zapf_broad(idx, from, opcode, par);
}

/* Show motd to someone
 */
static void bot_motd(int idx, char *par)
{
  FILE *vv;
  char *s = TBUF, *who, *p;
  int i;
  struct flag_record fr = { FR_BOT, USER_BOT, 0, 0, 0, 0 };

  who = newsplit(&par);
  if (!par[0] || !egg_strcasecmp(par, botnetnick)) {
    int irc = 0;

    p = strchr(who, ':');
    if (p)
      p++;
    else
      p = who;
    if (who[0] == '!') {
      irc = HELP_IRC;
      fr.global |=USER_HIGHLITE;

      who++;
    } else if (who[0] == '#') {
      fr.global |=USER_HIGHLITE;

      who++;
    }
    putlog(LOG_CMDS, "*", "#%s# motd", p);
    vv = fopen(motdfile, "r");
    if (vv != NULL) {
      botnet_send_priv(idx, botnetnick, who, NULL, _("--- MOTD file:\n"));
      help_subst(NULL, NULL, 0, irc, NULL);
      while (!feof(vv)) {
        fgets(s, 120, vv);
        if (!feof(vv)) {
          if (s[strlen(s) - 1] == '\n')
            s[strlen(s) - 1] = 0;
          if (!s[0])
            strcpy(s, " ");
          help_subst(s, who, &fr, HELP_DCC, dcc[idx].nick);
          if (s[0])
            botnet_send_priv(idx, botnetnick, who, NULL, "%s", s);
        }
      }
      fclose(vv);
    } else
      botnet_send_priv(idx, botnetnick, who, NULL, _("No MOTD file. :("));
  } else {
    /* Pass it on */
    i = nextbot(par);
    if (i >= 0)
      botnet_send_motd(i, who, par);
  }
}

/* These are still here, so that they will pass the relevant
 * requests through even if no filesys is loaded.
 *
 * filereject <bot:filepath> <sock:nick@bot> <reason...>
 */
static void bot_filereject(int idx, char *par)
{
  char *path, *to, *tobot, *p;
  int i;

  path = newsplit(&par);
  to = newsplit(&par);
  if ((tobot = strchr(to, '@')))
    tobot++;
  else
    tobot = to;                 /* Bot wants a file?! :) */
  if (egg_strcasecmp(tobot, botnetnick)) {      /* for me! */
    p = strchr(to, ':');
    if (p != NULL) {
      *p = 0;
      for (i = 0; i < dcc_total; i++) {
        if (dcc[i].sock == atoi(to))
          dprintf(i, _("FILE TRANSFER REJECTED (%s): %s\n"), path, par);
      }
      *p = ':';
    }
    /* No ':'? malformed */
    putlog(LOG_FILES, "*", _("%s rejected: %s"), path, par);
  } else {                        /* Pass it on */
    i = nextbot(tobot);
    if (i >= 0)
      botnet_send_filereject(i, path, to, par);
  }
}

/* filereq <sock:nick@bot> <bot:file>
 */
static void bot_filereq(int idx, char *tobot)
{
  char *from, *path;
  int i;

  from = newsplit(&tobot);
  if ((path = strchr(tobot, ':'))) {
    *path++ = 0;

    if (!egg_strcasecmp(tobot, botnetnick)) {   /* For me! */
      /* Process this */
      module_entry *fs = module_find("filesys", 0, 0);

      if (fs == NULL)
        botnet_send_priv(idx, botnetnick, from, NULL, _("Filesys module not loaded."));
      else {
        Function f = fs->funcs[FILESYS_REMOTE_REQ];

        f(idx, from, path);
      }
    } else {                      /* Pass it on */
      i = nextbot(tobot);
      if (i >= 0)
        botnet_send_filereq(i, from, tobot, path);
    }
  }
}

/* filesend <bot:path> <sock:nick@bot> <IP#> <port> <size>
 */
static void bot_filesend(int idx, char *par)
{
  char *botpath, *to, *tobot, *nick;
  int i;
  char *nfn;

  botpath = newsplit(&par);
  to = newsplit(&par);
  if ((tobot = strchr(to, '@'))) {
    *tobot = 0;
    tobot++;
  } else
    tobot = to;
  if (!egg_strcasecmp(tobot, botnetnick)) {     /* For me! */
    nfn = strrchr(botpath, '/');
    if (nfn == NULL) {
      nfn = strrchr(botpath, ':');
      if (nfn == NULL)
        nfn = botpath;          /* That's odd. */
      else
        nfn++;
    } else
      nfn++;
    if ((nick = strchr(to, ':')))
      nick++;
    else
      nick = to;
    /* Send it to 'nick' as if it's from me */
    dprintf(DP_SERVER, "PRIVMSG %s :\001DCC SEND %s %s\001\n", nick, nfn, par);
  } else {
    i = nextbot(tobot);
    if (i >= 0) {
      *--tobot = '@';
      botnet_send_filesend(i, botpath, to, par);
    }
  }
}

static void bot_error(int idx, char *par)
{
  putlog(LOG_MISC | LOG_BOTS, "*", "%s: %s", dcc[idx].nick, par);
}

/* nc <bot> <sock> <newnick>
 */
static void bot_nickchange(int idx, char *par)
{
  char *bot, *ssock, *newnick;
  int sock, i;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET) {
    fake_alert(idx, "botversion", "NEAT_BOTNET");
    return;
  }
#endif
  i = nextbot(bot);
  if (i != idx) {
    fake_alert(idx, "direction", bot);
    return;
  }
  ssock = newsplit(&par);
  sock = base64_to_int(ssock);
  newnick = newsplit(&par);
  i = partynick(bot, sock, newnick);
  if (i < 0) {
    fake_alert(idx, "sock#", ssock);
    return;
  }
  chanout_but(-1, party[i].chan, _("*** (%s) Nick change: %s -> %s\n"),
              bot, newnick, party[i].nick);
  botnet_send_nkch_part(idx, i, newnick);
}

/* join <bot> <nick> <chan> <flag><sock> <from>
 */
static void bot_join(int idx, char *par)
{
  char *bot, *nick, *x, *y;
  struct userrec *u;
  int i, sock, chan, i2, linking = 0;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) >= NEAT_BOTNET)
#endif
    if (bot[0] == '!') {
      linking = 1;
      bot++;
    }
  if (b_status(idx) & STAT_LINKING) {
    linking = 1;
  }
  nick = newsplit(&par);
  x = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    chan = atoi(x);
  else
#endif
    chan = base64_to_int(x);
  y = newsplit(&par);
  if ((chan < 0) || !y[0])
    return;                     /* Woops! pre 1.2.1's send .chat off'ers
                                 * too!! */
  if (!y[0]) {
    y[0] = '-';
    sock = 0;
  } else {
#ifndef NO_OLD_BOTNET
    if (b_numver(idx) < NEAT_BOTNET)
      sock = atoi(y + 1);
    else
#endif
      sock = base64_to_int(y + 1);
  }
  /* 1.1 bots always send a sock#, even on a channel change
   * so if sock# is 0, this is from an old bot and we must tread softly
   * grab old sock# if there is one, otherwise make up one.
   */
  if (sock == 0)
    sock = partysock(bot, nick);
  if (sock == 0)
    sock = fakesock++;
  i = nextbot(bot);
  if (i != idx) {               /* Ok, garbage from a 1.0g (who uses that
                                 * now?) OR raistlin being evil :) */
    fake_alert(idx, "direction", bot);
    return;
  }
  u = get_user_by_handle(userlist, nick);
  if (u) {
    sprintf(TBUF, "@%s", bot);
    touch_laston(u, TBUF, now);
  }
  i = addparty(bot, nick, chan, y[0], sock, par, &i2);
  botnet_send_join_party(idx, linking, i2, i);
  if (i != chan) {
    if (i >= 0) {
      if (b_numver(idx) >= NEAT_BOTNET) {
        if (i)
          chanout_but(-1, i, _("*** (%s) %s has left the channel"));
        else
          chanout_but(-1, i, _("*** (%s) %s has left the party line"));
      }
      check_tcl_chpt(bot, nick, sock, i);
    }
    if ((b_numver(idx) >= NEAT_BOTNET) && !linking) {
        if (chan)
          chanout_but(-1, chan, _("*** (%s) %s has joined the channel.\n"),
                      bot, nick);
        else
          chanout_but(-1, chan, _("*** (%s) %s has joined the party line.\n"),
                      bot, nick);
    }
    check_tcl_chjn(bot, nick, chan, y[0], sock, par);
  }
}

/* part <bot> <nick> <sock> [etc..]
 */
static void bot_part(int idx, char *par)
{
  char *bot, *nick, *etc;
  struct userrec *u;
  int sock, partyidx;
  int silent = 0;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
  if (bot[0] == '!') {
    silent = 1;
    bot++;
  }
  nick = newsplit(&par);
  etc = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET) {
    sock = atoi(etc);
    silent = 1;
  } else
#endif
    sock = base64_to_int(etc);
  if (sock == 0)
    sock = partysock(bot, nick);
  u = get_user_by_handle(userlist, nick);
  if (u) {
    sprintf(TBUF, "@%s", bot);
    touch_laston(u, TBUF, now);
  }
  if ((partyidx = getparty(bot, sock)) != -1) {
    if (party[partyidx].chan >= 0)
      check_tcl_chpt(bot, nick, sock, party[partyidx].chan);
    if ((b_numver(idx) >= NEAT_BOTNET) && !silent) {
      register int chan = party[partyidx].chan;

      if (par[0]) {
        if (chan)
          chanout_but(-1, chan, _("*** (%s) %s has left the channel (%s).\n"),
                      bot, nick, par);
        else
          chanout_but(-1, chan, _("*** (%s) %s has left the partyline (%s).\n"),
                      bot, nick, par);
      }
    }
    botnet_send_part_party(idx, partyidx, par, silent);
    remparty(bot, sock);
  }
}

/* away <bot> <sock> <message>
 * null message = unaway
 */
static void bot_away(int idx, char *par)
{
  char *bot, *etc;
  int sock, partyidx, linking = 0;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) >= NEAT_BOTNET)
#endif
    if (bot[0] == '!') {
      linking = 1;
      bot++;
    }
  if (b_status(idx) & STAT_LINKING) {
    linking = 1;
  }
  etc = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    sock = atoi(etc);
  else
#endif
    sock = base64_to_int(etc);
  if (sock == 0)
    sock = partysock(bot, etc);
  check_tcl_away(bot, sock, par);
  if (par[0]) {
    partystat(bot, sock, PLSTAT_AWAY, 0);
    partyaway(bot, sock, par);
  } else
    partystat(bot, sock, 0, PLSTAT_AWAY);
  partyidx = getparty(bot, sock);
  if ((b_numver(idx) >= NEAT_BOTNET) && !linking) {
    if (par[0])
      chanout_but(-1, party[partyidx].chan,
                  _("*** (%s) %s is now away: %s.\n"), bot,
                  party[partyidx].nick, par);
    else
      chanout_but(-1, party[partyidx].chan,
                  _("*** (%s) %s is no longer away.\n"), bot,
                  party[partyidx].nick);
  }
  botnet_send_away(idx, bot, sock, par, linking);
}

/* (a courtesy info to help during connect bursts)
 * idle <bot> <sock> <#secs> [away msg]
 */
static void bot_idle(int idx, char *par)
{
  char *bot, *work;
  int sock, idle;

  if (bot_flags(dcc[idx].user) & BOT_ISOLATE)
    return;
  bot = newsplit(&par);
  work = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    sock = atoi(work);
  else
#endif
    sock = base64_to_int(work);
  if (sock == 0)
    sock = partysock(bot, work);
  work = newsplit(&par);
#ifndef NO_OLD_BOTNET
  if (b_numver(idx) < NEAT_BOTNET)
    idle = atoi(work);
  else
#endif
    idle = base64_to_int(work);
  partysetidle(bot, sock, idle);
  if (par[0]) {
    partystat(bot, sock, PLSTAT_AWAY, 0);
    partyaway(bot, sock, par);
  }
  botnet_send_idle(idx, bot, sock, idle, par);
}

#ifndef NO_OLD_BOTNET

static void bot_ufno(int idx, char *par)
{
  putlog(LOG_BOTS, "*", _("User file rejected by %s: %s"), dcc[idx].nick, par);
  dcc[idx].status &= ~STAT_OFFERED;
  if (!(dcc[idx].status & STAT_GETTING))
    dcc[idx].status &= ~STAT_SHARE;
}

static void bot_old_userfile(int idx, char *par)
{
  putlog(LOG_BOTS, "*", _("Old style share request by %s"), dcc[idx].nick);
  dprintf(idx, "uf-no %s\n", _("Antiquated sharing request"));
}

#endif /* !NO_OLD_BOTNET */

void bot_share(int idx, char *par)
{
  sharein(idx, par);
}

/* v <frombot> <tobot> <idx:nick>
 */
static void bot_versions(int sock, char *par)
{
  char *frombot = newsplit(&par), *tobot, *from;
  module_entry *me;

  if (nextbot(frombot) != sock)
    fake_alert(sock, "versions-direction", frombot);
  else if (egg_strcasecmp(tobot = newsplit(&par), botnetnick)) {
    if ((sock = nextbot(tobot)) >= 0)
      dprintf(sock, "v %s %s %s\n", frombot, tobot, par);
  } else {
    from = newsplit(&par);
    botnet_send_priv(sock, botnetnick, from, frombot, _("Modules loaded:\n"));
    for (me = module_list; me; me = me->next)
      botnet_send_priv(sock, botnetnick, from, frombot,
                       _("  Module: %s (v%d.%d)\n"), me->name, me->major,
                       me->minor);
    botnet_send_priv(sock, botnetnick, from, frombot,
                     _("End of module list.\n"));
  }
}

#ifdef TLS
/* Negotiate an encrypted session over the existing link
 * starttls
 */
static void bot_starttls(int idx, char *par)
{
  /* We're already using ssl, ignore the request */
  if (dcc[idx].ssl)
    return;

  if (dcc[idx].status & STAT_STARTTLS) {
    /* we requested ssl, now we got the reply */
    dcc[idx].status &= ~STAT_STARTTLS;
    ssl_handshake(dcc[idx].sock, TLS_CONNECT, tls_vfybots, LOG_BOTS,
                  dcc[idx].host, NULL);
  } else {
    /* the peer requests ssl, tell it to go on */
    /*
      if (!SSL_CTX_check_private_key(ssl_ctx)) {
      putlog(LOG_BOTS, "*", "%s", ERR_error_string(ERR_get_error()));
      return;
    }
    */
    dprintf(idx, "starttls\n");
    putlog(LOG_BOTS, "*", _("Got STARTTLS from %s. Replying..."),
           dcc[idx].nick);
    ssl_handshake(dcc[idx].sock, TLS_LISTEN, tls_vfybots, LOG_BOTS,
                  dcc[idx].host, NULL);
  }
  dcc[idx].ssl = 1;
}
#endif

/* BOT COMMANDS
 *
 * function call should be:
 * int bot_whatever(idx,"parameters");
 *
 * SORT these, dcc_bot uses a shortcut which requires them sorted
 *
 * yup, those are tokens there to allow a more efficient botnet as
 * time goes on (death to slowly upgrading llama's)
 */
botcmd_t C_bot[] =
{
  {"a",          (IntFunc) bot_actchan},
#ifndef NO_OLD_BOTNET
  {"actchan",    (IntFunc) bot_actchan},
#endif
  {"aw",         (IntFunc) bot_away},
  {"away",       (IntFunc) bot_away},
  {"bye",        (IntFunc) bot_bye},
  {"c",          (IntFunc) bot_chan2},
#ifndef NO_OLD_BOTNET
  {"chan",       (IntFunc) bot_chan2},
  {"chat",       (IntFunc) bot_chat},
#endif
  {"ct",         (IntFunc) bot_chat},
  {"e",          (IntFunc) bot_error},
  {"el",         (IntFunc) bot_endlink},
#ifndef NO_OLD_BOTNET
  {"error",      (IntFunc) bot_error},
#endif
  {"f!",         (IntFunc) bot_filereject},
#ifndef NO_OLD_BOTNET
  {"filereject", (IntFunc) bot_filereject},
  {"filereq",    (IntFunc) bot_filereq},
  {"filesend",   (IntFunc) bot_filesend},
#endif
  {"fr",         (IntFunc) bot_filereq},
  {"fs",         (IntFunc) bot_filesend},
  {"h",          (IntFunc) bot_handshake},
#ifndef NO_OLD_BOTNET
  {"handshake",  (IntFunc) bot_handshake},
#endif
  {"i",          (IntFunc) bot_idle},
  {"i?",         (IntFunc) bot_infoq},
#ifndef NO_OLD_BOTNET
  {"idle",       (IntFunc) bot_idle},
  {"info?",      (IntFunc) bot_infoq},
#endif
  {"j",          (IntFunc) bot_join},
#ifndef NO_OLD_BOTNET
  {"join",       (IntFunc) bot_join},
#endif
  {"l",          (IntFunc) bot_link},
#ifndef NO_OLD_BOTNET
  {"link",       (IntFunc) bot_link},
  {"linked",     (IntFunc) bot_linked},
#endif
  {"m",          (IntFunc) bot_motd},
#ifndef NO_OLD_BOTNET
  {"motd",       (IntFunc) bot_motd},
#endif
  {"n",          (IntFunc) bot_nlinked},
  {"nc",         (IntFunc) bot_nickchange},
#ifndef NO_OLD_BOTNET
  {"nlinked",    (IntFunc) bot_nlinked},
#endif
  {"p",          (IntFunc) bot_priv},
#ifndef NO_OLD_BOTNET
  {"part",       (IntFunc) bot_part},
#endif
  {"pi",         (IntFunc) bot_ping},
#ifndef NO_OLD_BOTNET
  {"ping",       (IntFunc) bot_ping},
#endif
  {"po",         (IntFunc) bot_pong},
#ifndef NO_OLD_BOTNET
  {"pong",       (IntFunc) bot_pong},
  {"priv",       (IntFunc) bot_priv},
#endif
  {"pt",         (IntFunc) bot_part},
  {"r",          (IntFunc) bot_reject},
#ifndef NO_OLD_BOTNET
  {"reject",     (IntFunc) bot_reject},
#endif
  {"s",          (IntFunc) bot_share},
#ifdef TLS
  {"starttls",   (IntFunc) bot_starttls},
#endif
  {"t",          (IntFunc) bot_trace},
  {"tb",         (IntFunc) bot_thisbot},
  {"td",         (IntFunc) bot_traced},
#ifndef NO_OLD_BOTNET
  {"thisbot",    (IntFunc) bot_thisbot},
  {"trace",      (IntFunc) bot_trace},
  {"traced",     (IntFunc) bot_traced},
#endif
  {"u",          (IntFunc) bot_update},
#ifndef NO_OLD_BOTNET
  {"uf-no",      (IntFunc) bot_ufno},
#endif
  {"ul",         (IntFunc) bot_unlink},
  {"un",         (IntFunc) bot_unlinked},
#ifndef NO_OLD_BOTNET
  {"unaway",     (IntFunc) bot_away},
  {"unlink",     (IntFunc) bot_unlink},
  {"unlinked",   (IntFunc) bot_unlinked},
  {"update",     (IntFunc) bot_update},
  {"userfile?",  (IntFunc) bot_old_userfile},
#endif
  {"v",          (IntFunc) bot_versions},
  {"w",          (IntFunc) bot_who},
#ifndef NO_OLD_BOTNET
  {"who",        (IntFunc) bot_who},
#endif
  {"z",          (IntFunc) bot_zapf},
#ifndef NO_OLD_BOTNET
  {"zapf",       (IntFunc) bot_zapf},
  {"zapf-broad", (IntFunc) bot_zapfbroad},
#endif
  {"zb",         (IntFunc) bot_zapfbroad},
  {NULL,         NULL}
};
