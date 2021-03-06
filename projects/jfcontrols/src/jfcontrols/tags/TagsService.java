package jfcontrols.tags;

/** Tags Service
 *
 * @author pquiring
*/

import java.util.*;

import javaforce.*;
import javaforce.controls.TagType;

import jfcontrols.sql.*;

public class TagsService extends Thread {
  private static Object lock_main = new Object();
  private static Object lock_signal = new Object();
  private static Object lock_tags = new Object();
  private static Object lock_done_reads = new Object();
  private static Object lock_done_writes = new Object();
  private static TagsService service;
  private static volatile boolean active;
  private static volatile boolean doReads;
  private static volatile boolean doWrites;

  private HashMap<String, TagBase> localTags = new HashMap<>();
  private HashMap<String, TagBase> remoteTags = new HashMap<>();

  public static String read(String tag) {
    TagAddr ta = TagAddr.decode(tag);
    return getTag(ta).getValue(ta);
  }

  public static void write(String tag, String value) {
    TagAddr ta = TagAddr.decode(tag);
    getTag(ta).setValue(ta, value);
  }

  public static TagBase getTag(TagAddr ta) {
    return service.findTag(ta);
  }

  private TagBase findTag(TagAddr ta) {
    if (ta.tempValue != null) {
      return new TagTemp(ta.tempValue);
    }
    synchronized(lock_tags) {
      if (ta.cid == 0) {
        return localTags.get(ta.name);
      } else {
        return remoteTags.get(ta.name);
      }
    }
  }

  public static void main() {
    synchronized(lock_main) {
      new TagsService().start();
      try {lock_main.wait();} catch (Exception e) {}
    }
  }

  public void run() {
    service = this;
    SQL sql = SQLService.getSQL();
    String tags[][] = sql.select("select name,type,cid,unsigned,array from tags");
    for(int a=0;a<tags.length;a++) {
      if (tags[a][2].equals("0")) {
        int type = Integer.valueOf(tags[a][1]);
        if (tags[a][0].equals("io"))
          localTags.put(tags[a][0], new IOTag(0, tags[a][0], type, tags[a][3].equals("true"), tags[a][4].equals("true"), sql));
        else
          localTags.put(tags[a][0], new LocalTag(0, tags[a][0], type, tags[a][3].equals("true"), tags[a][4].equals("true"), sql));
      } else {
        remoteTags.put("c" + tags[a][2] + "#" + tags[a][0], new RemoteTag(Integer.valueOf(tags[a][2]), tags[a][0], Integer.valueOf(tags[a][1]), tags[a][3].equals("true"), false, sql));
      }
    }
    active = true;
    synchronized(lock_main) {
      lock_main.notify();  //signal main to continue
    }
    synchronized(lock_signal) {
      while (active) {
        try {lock_signal.wait();} catch (Exception e) {}
        if (doReads) {
          processReads(sql);
        }
        if (doWrites) {
          processWrites(sql);
        }
      }
    }
    service = null;
    sql.close();
  }
  private void processReads(SQL sql) {
    MonitoredTag localtags[] = (MonitoredTag[])localTags.values().toArray(new MonitoredTag[0]);
    for(int a=0;a<localtags.length;a++) {
      localtags[a].updateRead(sql);
    }
    MonitoredTag remotetags[] = (MonitoredTag[])remoteTags.values().toArray(new MonitoredTag[0]);
    for(int a=0;a<remotetags.length;a++) {
      remotetags[a].updateRead(sql);
    }
    synchronized(lock_done_reads) {
      doReads = false;
      lock_done_reads.notify();
    }
  }
  private void processWrites(SQL sql) {
    MonitoredTag localtags[] = (MonitoredTag[])localTags.values().toArray(new MonitoredTag[0]);
    for(int a=0;a<localtags.length;a++) {
      localtags[a].updateWrite(sql);
    }
    MonitoredTag remotetags[] = (MonitoredTag[])remoteTags.values().toArray(new MonitoredTag[0]);
    for(int a=0;a<remotetags.length;a++) {
      remotetags[a].updateWrite(sql);
    }
    synchronized(lock_done_writes) {
      doWrites = false;
      lock_done_writes.notify();
    }
  }
  public static void doReads() {
    synchronized(lock_done_reads) {
      synchronized(lock_signal) {
        doReads = true;
        lock_signal.notify();
      }
      try {lock_done_reads.wait();} catch (Exception e) {}
    }
  }
  public static void doWrites() {
    synchronized(lock_done_writes) {
      synchronized(lock_signal) {
        doWrites = true;
        lock_signal.notify();
      }
      try {lock_done_writes.wait();} catch (Exception e) {}
    }
  }
  public static void cancel() {
    synchronized(lock_signal) {
      active = false;
      lock_signal.notify();
    }
  }
}
