<?xml version="1.0"?>
<!-- an XHTML result (output method xml): parsed as XML again, its script run -->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns="http://www.w3.org/1999/xhtml">
<xsl:output method="xml" omit-xml-declaration="yes"/>
<xsl:template match="/list">
  <html>
    <head><title>Sorted list</title></head>
    <body>
      <ol id="ol">
        <xsl:for-each select="item">
          <xsl:sort select="@n" data-type="number"/>
          <li><xsl:number value="@n" format="a"/>. <xsl:value-of select="."/></li>
        </xsl:for-each>
      </ol>
      <script type="text/javascript">
        var li = document.getElementsByTagName("li");
        console.log("XMLT xslt xml result " + li.length + " " + li[0].textContent + " " + document.contentType);
      </script>
    </body>
  </html>
</xsl:template>
</xsl:stylesheet>
