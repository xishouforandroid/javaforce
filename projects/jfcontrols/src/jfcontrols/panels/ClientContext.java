package jfcontrols.panels;

/** Monitors tag changes for components on Panel.
 *
 * @author pquiring
 */

import java.util.*;

import javaforce.webui.*;

import jfcontrols.tags.*;

public class ClientContext extends Thread {
  private WebUIClient client;
  private ArrayList<Pair> listeners = new ArrayList<>();
  private volatile boolean active;
  private Object lock = new Object();
  private ArrayList<Pair> stack = new ArrayList<>();

  public ClientContext(WebUIClient client) {
    this.client = client;
  }
  private static class Pair implements TagListener {
    public MonitoredTag tag;
    public Component comp;
    public ClientContext ctx;
    public String value;
    public Pair(MonitoredTag tag, Component comp, ClientContext ctx) {
      this.tag = tag;
      this.comp = comp;
      this.ctx = ctx;
    }
    public void tagChanged(Tag tag, String value) {
      synchronized(ctx.lock) {
        this.value = value;
        ctx.stack.add(this);
        ctx.notify();
      }
    }
  }

  public void addListener(MonitoredTag tag, Component comp) {
    Pair pair = new Pair(tag, comp, this);
    listeners.add(pair);
    tag.addListener(pair);
  }

  public void clear() {
    while (listeners.size() > 0) {
      Pair pair = listeners.remove(0);
      pair.tag.removeListener(pair);
    }
  }

  public void run() {
    Pair pair;
    active = true;
    while (active) {
      synchronized(lock) {
        try {lock.wait();} catch (Exception e) {}
        pair = stack.remove(0);
      }
      if (pair == null) continue;
      Component c = pair.comp;
      if (c instanceof Label) {
        ((Label)c).setText(pair.value);
      }
    }
  }

  public void cancel() {
    active = false;
    synchronized(lock) {
      lock.notify();
    }
  }
}