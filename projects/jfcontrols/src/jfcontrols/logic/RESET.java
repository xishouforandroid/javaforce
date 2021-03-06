package jfcontrols.logic;

/** Clears bit.
 *
 * @author pquiring
 */

import javaforce.controls.*;

public class RESET extends Logic {

  public boolean isBlock() {
    return false;
  }

  public String getName() {
    return "Reset";
  }

  public String getCode() {
    return "if (enabled) setBoolean(tags[1], false);\r\n";
  }

  public int getTagsCount() {
    return 1;
  }

  public int getTagType(int idx) {
    return TagType.bit;
  }
}
