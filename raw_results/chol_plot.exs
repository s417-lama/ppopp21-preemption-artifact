filename = System.argv() |> Enum.at(0)

linewidth = 2

nts = [8, 12, 16, 20, 24]
nts_name = ["8x8", "12x12", "16x16", "20x20", "24x24"]

files = [
    {"bolt_nonpreemptive"         , "BOLT<br>(nonpreemptive,<br>reverse-engineered)"},
    {"bolt_preemptive_intvl_10000", "BOLT<br>(preemptive,<br>intvl=10ms)"           },
    {"bolt_preemptive_intvl_1000" , "BOLT<br>(preemptive,<br>intvl=1ms)"            },
    {"iomp"                       , "<br>IOMP<br>"                                  },
    {"iomp_flat"                  , "<br>IOMP (flat)<br>"                           },
]

files
|> Enum.map(fn {name, title} ->
    {ys, errors} =
        Enum.map(nts, fn nt ->
            data =
                File.stream!("chol/#{name}_nt_#{nt}.out")
                |> Stream.map(&String.split/1)
                |> Stream.map(fn
                    [gflops, "GFLOPS"] -> String.to_integer(gflops)
                    _ -> nil
                end)
                |> Stream.filter(&!is_nil(&1))
                |> Enum.to_list()
            {Statistics.mean(data), Statistics.stdev(data)}
        end)
        |> Enum.unzip()
    %{
        x: nts_name,
        y: ys,
        error_y: %{
            type:      "data",
            array:     errors,
            visible:   true,
            thickness: linewidth,
        },
        type: "bar",
        name: title,
    }
end)
|> PlotlyEx.plot(%{
    width:  800,
    height: 400,
    xaxis: %{
        title: %{text: "# of Tiles"},
    },
    yaxis: %{
        title: %{text: "GFLOPS"},
    },
    font: %{
        size: 20,
    },
    margin: %{
        l: 90,
        r: 0,
        b: 60,
        t: 10,
        pad: 0,
    },
    legend: %{
        orientation: "h",
        x: -0.1,
        y: 1.4,
    },
})
|> PlotlyEx.show(filename: filename)
